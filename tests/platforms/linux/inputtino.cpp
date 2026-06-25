#include "libinput.h"
#include <boost/endian/conversion.hpp>
#include <boost/locale.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>
#include <chrono>
#include <control/input_handler.hpp>
#include <immer/box.hpp>
#include <inputtino/input.hpp>
#include <platforms/input.hpp>
#include <platforms/linux/uinput/uinput.hpp>
#include <thread>

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::Equals;
using Catch::Matchers::StartsWith;
using Catch::Matchers::WithinRel;

using namespace wolf::core::input;
using namespace wolf::core;
using namespace moonlight::control;
using namespace std::string_literals;

TEST_CASE("uinput - keyboard", "[UINPUT]") {
  libevdev_ptr keyboard_dev(libevdev_new(), ::libevdev_free);
  auto session = events::StreamSession{
      .keyboard = std::make_shared<std::optional<events::KeyboardTypes>>(std::move(*Keyboard::create()))};
  link_devnode(keyboard_dev.get(), std::get<state::input::Keyboard>(session.keyboard->value()).get_nodes()[0]);

  auto events = fetch_events_debug(keyboard_dev);
  REQUIRE(events.empty());

  auto press_shift_key = pkts::KEYBOARD_PACKET{.key_code = boost::endian::native_to_little((short)0xA0)};
  press_shift_key.type = pkts::KEY_PRESS;

  control::handle_input(session, {}, &press_shift_key);
  events = fetch_events_debug(keyboard_dev);
  REQUIRE(events.size() == 1);
  REQUIRE_THAT(libevdev_event_type_get_name(events[0]->type), Equals("EV_KEY"));
  REQUIRE_THAT(libevdev_event_code_get_name(events[0]->type, events[0]->code), Equals("KEY_LEFTSHIFT"));
  REQUIRE(events[0]->value == 1);

  auto release_shift_key = pkts::KEYBOARD_PACKET{.key_code = boost::endian::native_to_little((short)0xA0)};
  release_shift_key.type = pkts::KEY_RELEASE;

  control::handle_input(session, {}, &release_shift_key);
  events = fetch_events_debug(keyboard_dev);
  REQUIRE(events.size() == 1);
  REQUIRE_THAT(libevdev_event_type_get_name(events[0]->type), Equals("EV_KEY"));
  REQUIRE_THAT(libevdev_event_code_get_name(events[0]->type, events[0]->code), Equals("KEY_LEFTSHIFT"));
  REQUIRE(events[0]->value == 0);
}

TEST_CASE("uinput - pen tablet", "[UINPUT]") {
  auto session = events::StreamSession{.event_bus = std::make_shared<events::EventBusType>()};
  auto packet = pkts::PEN_PACKET{.event_type = pkts::TOUCH_EVENT_HOVER,
                                 .tool_type = pkts::TOOL_TYPE_PEN,
                                 .pen_buttons = pkts::PEN_BUTTON_TYPE_PRIMARY,
                                 .x = {0, 0, 0, 63},                    // 0.5 in float (little endian)
                                 .y = {0, 0, 0, 63},                    // 0.5 in float (little endian)
                                 .pressure_or_distance = {0, 0, 0, 63}, // 0.5 in float (little endian)
                                 .rotation = 0,
                                 .tilt = 0};
  packet.type = pkts::PEN;

  control::handle_input(session, {}, &packet);

  REQUIRE(session.pen_tablet->has_value()); // Should create a pen when a packet arrives
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  auto li = create_libinput_context(session.pen_tablet->value().get_nodes());
  auto event = get_event(li);
  REQUIRE(event);
  REQUIRE(libinput_event_get_type(event.get()) == LIBINPUT_EVENT_DEVICE_ADDED);
  REQUIRE(libinput_device_has_capability(libinput_event_get_device(event.get()), LIBINPUT_DEVICE_CAP_TABLET_TOOL));

  // Changing something just so that libinput picks up the event
  packet.rotation = 90;
  packet.tilt = 45;
  control::handle_input(session, {}, &packet);

  float TARGET_W = 1920;
  float TARGET_H = 1080;
  {
    event = get_event(li);
    REQUIRE(event);
    REQUIRE(libinput_event_get_type(event.get()) == LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY);
    auto t_event = libinput_event_get_tablet_tool_event(event.get());
    REQUIRE(libinput_event_tablet_tool_get_proximity_state(t_event) == LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_IN);
    REQUIRE(libinput_tablet_tool_get_type(libinput_event_tablet_tool_get_tool(t_event)) ==
            LIBINPUT_TABLET_TOOL_TYPE_PEN);
    REQUIRE(libinput_event_tablet_tool_get_distance(t_event) == 0.5);
    REQUIRE(libinput_event_tablet_tool_get_pressure(t_event) == 0.0);
    REQUIRE_THAT(libinput_event_tablet_tool_get_x_transformed(t_event, TARGET_W), WithinRel(TARGET_W * 0.5f, 0.5f));
    REQUIRE_THAT(libinput_event_tablet_tool_get_y_transformed(t_event, TARGET_H), WithinRel(TARGET_H * 0.5f, 0.5f));
    REQUIRE_THAT(libinput_event_tablet_tool_get_tilt_x(t_event), WithinRel(-45.0f, 0.1f));
    REQUIRE_THAT(libinput_event_tablet_tool_get_tilt_y(t_event),
                 WithinRel(0.0f, 0.1f)); // 90° rotation means 0° tilt Y (full right position)
    REQUIRE(libinput_event_tablet_tool_get_tip_state(t_event) == LIBINPUT_TABLET_TOOL_TIP_UP);
  }

  packet = pkts::PEN_PACKET{.event_type = pkts::TOUCH_EVENT_DOWN,
                            .tool_type = pkts::TOOL_TYPE_PEN,
                            .pen_buttons = pkts::PEN_BUTTON_TYPE_PRIMARY,
                            .x = {0, 0, 0, 63},                    // 0.5 in float (little endian)
                            .y = {0, 0, 0, 63},                    // 0.5 in float (little endian)
                            .pressure_or_distance = {0, 0, 0, 63}, // 0.5 in float (little endian)
                            .rotation = 180,
                            .tilt = 90};
  packet.type = pkts::PEN;

  control::handle_input(session, {}, &packet);
  {
    event = get_event(li);
    REQUIRE(libinput_event_get_type(event.get()) == LIBINPUT_EVENT_TABLET_TOOL_TIP);
    auto t_event = libinput_event_get_tablet_tool_event(event.get());
    REQUIRE(libinput_event_tablet_tool_get_proximity_state(t_event) == LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_IN);
    REQUIRE(libinput_tablet_tool_get_type(libinput_event_tablet_tool_get_tool(t_event)) ==
            LIBINPUT_TABLET_TOOL_TYPE_PEN);
    REQUIRE(libinput_event_tablet_tool_get_distance(t_event) == 0.0);
    REQUIRE_THAT(libinput_event_tablet_tool_get_pressure(t_event), WithinRel(0.5f, 0.1f));
    REQUIRE_THAT(libinput_event_tablet_tool_get_x_transformed(t_event, TARGET_W), WithinRel(TARGET_W * 0.5f, 0.5f));
    REQUIRE_THAT(libinput_event_tablet_tool_get_y_transformed(t_event, TARGET_H), WithinRel(TARGET_H * 0.5f, 0.5f));
    REQUIRE_THAT(libinput_event_tablet_tool_get_tilt_x(t_event), WithinRel(90.0f, 0.1f));
    REQUIRE_THAT(libinput_event_tablet_tool_get_tilt_y(t_event),
                 WithinRel(-90.0f, 0.1f)); // 180° rotation means -90° tilt Y (full down position)
    REQUIRE(libinput_event_tablet_tool_get_tip_state(t_event) == LIBINPUT_TABLET_TOOL_TIP_DOWN);
  }
}

TEST_CASE("uinput - touch screen", "[UINPUT]") {
  auto session = events::StreamSession{.event_bus = std::make_shared<events::EventBusType>()};

  auto packet = pkts::TOUCH_PACKET{
      .event_type = pkts::TOUCH_EVENT_UP,
      .pointer_id = boost::endian::native_to_little(0),
      .x = {0, 0, 0, 63},                    // 0.5 in float (little endian)
      .y = {0, 0, 0, 63},                    // 0.5 in float (little endian)
      .pressure_or_distance = {0, 0, 0, 63}, // 0.5 in float (little endian)
  };
  packet.type = pkts::TOUCH;

  control::handle_input(session, {}, &packet); // Should create a touch screen when a packet arrives
  REQUIRE(session.touch_screen->has_value());
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  auto li = create_libinput_context(std::get<TouchScreen>(session.touch_screen->value()).get_nodes());
  auto event = get_event(li);
  REQUIRE(event);
  REQUIRE(libinput_event_get_type(event.get()) == LIBINPUT_EVENT_DEVICE_ADDED);
  REQUIRE(libinput_device_has_capability(libinput_event_get_device(event.get()), LIBINPUT_DEVICE_CAP_TOUCH));

  packet.event_type = pkts::TOUCH_EVENT_HOVER;
  control::handle_input(session, {}, &packet);

  float TARGET_W = 1920;
  float TARGET_H = 1080;
  {
    event = get_event(li);
    REQUIRE(libinput_event_get_type(event.get()) == LIBINPUT_EVENT_TOUCH_DOWN);
    auto t_event = libinput_event_get_touch_event(event.get());
    REQUIRE(libinput_event_touch_get_slot(t_event) == 1);
    REQUIRE_THAT(libinput_event_touch_get_x_transformed(t_event, TARGET_W), WithinRel(TARGET_W * 0.5f, 0.5f));
    REQUIRE_THAT(libinput_event_touch_get_y_transformed(t_event, TARGET_H), WithinRel(TARGET_H * 0.5f, 0.5f));
    event = get_event(li);
    REQUIRE(libinput_event_get_type(event.get()) == LIBINPUT_EVENT_TOUCH_FRAME);
  }

  packet = pkts::TOUCH_PACKET{
      .event_type = pkts::TOUCH_EVENT_UP,
      .pointer_id = boost::endian::native_to_little(0),
  };
  packet.type = pkts::TOUCH;

  control::handle_input(session, {}, &packet);

  {
    event = get_event(li);
    REQUIRE(libinput_event_get_type(event.get()) == LIBINPUT_EVENT_TOUCH_UP);
    auto t_event = libinput_event_get_touch_event(event.get());
    REQUIRE(libinput_event_touch_get_slot(t_event) == 1);
    event = get_event(li);
    REQUIRE(libinput_event_get_type(event.get()) == LIBINPUT_EVENT_TOUCH_FRAME);
  }
}

TEST_CASE("uinput - mouse", "[UINPUT]") {
  libevdev_ptr mouse_rel_dev(libevdev_new(), ::libevdev_free);
  libevdev_ptr mouse_abs_dev(libevdev_new(), ::libevdev_free);
  auto mouse = std::make_shared<std::optional<events::MouseTypes>>(std::move(*Mouse::create()));
  auto session = events::StreamSession{.display_mode = {.width = 1920, .height = 1080}, .mouse = mouse};

  link_devnode(mouse_rel_dev.get(), std::get<state::input::Mouse>(mouse->value()).get_nodes()[0]);
  link_devnode(mouse_abs_dev.get(), std::get<state::input::Mouse>(mouse->value()).get_nodes()[1]);

  auto events = fetch_events_debug(mouse_rel_dev);
  REQUIRE(events.empty());
  events = fetch_events_debug(mouse_abs_dev);
  REQUIRE(events.empty());

  SECTION("Mouse move") {
    auto mv_packet = pkts::MOUSE_MOVE_REL_PACKET{.delta_x = 10, .delta_y = 20};
    mv_packet.type = pkts::MOUSE_MOVE_REL;

    control::handle_input(session, {}, &mv_packet);
    events = fetch_events_debug(mouse_rel_dev);
    REQUIRE(events.size() == 2);
    REQUIRE_THAT(libevdev_event_type_get_name(events[0]->type), Equals("EV_REL"));
    REQUIRE_THAT(libevdev_event_code_get_name(events[0]->type, events[0]->code), Equals("REL_X"));
    REQUIRE(events[0]->value > 0);

    REQUIRE_THAT(libevdev_event_type_get_name(events[1]->type), Equals("EV_REL"));
    REQUIRE_THAT(libevdev_event_code_get_name(events[1]->type, events[1]->code), Equals("REL_Y"));
    REQUIRE(events[1]->value > 0);
  }

  SECTION("Mouse move absolute") {
    auto mv_packet = pkts::MOUSE_MOVE_ABS_PACKET{.x = boost::endian::native_to_big((short)100),
                                                 .y = boost::endian::native_to_big((short)200),
                                                 .width = boost::endian::native_to_big((short)1280),
                                                 .height = boost::endian::native_to_big((short)720)};
    mv_packet.type = pkts::MOUSE_MOVE_ABS;

    control::handle_input(session, {}, &mv_packet);
    events = fetch_events_debug(mouse_abs_dev);
    REQUIRE(events.size() == 2);
    REQUIRE_THAT(libevdev_event_type_get_name(events[0]->type), Equals("EV_ABS"));
    REQUIRE_THAT(libevdev_event_code_get_name(events[0]->type, events[0]->code), Equals("ABS_X"));
    REQUIRE(events[0]->value > 0);

    REQUIRE_THAT(libevdev_event_type_get_name(events[1]->type), Equals("EV_ABS"));
    REQUIRE_THAT(libevdev_event_code_get_name(events[1]->type, events[1]->code), Equals("ABS_Y"));
    REQUIRE(events[1]->value > 0);
  }

  SECTION("Mouse press button") {
    auto pressed_packet = pkts::MOUSE_BUTTON_PACKET{.button = 5};
    pressed_packet.type = pkts::MOUSE_BUTTON_PRESS;

    control::handle_input(session, {}, &pressed_packet);
    events = fetch_events_debug(mouse_rel_dev);
    REQUIRE(events.size() == 2);
    REQUIRE_THAT(libevdev_event_type_get_name(events[0]->type), Equals("EV_MSC"));
    REQUIRE_THAT(libevdev_event_code_get_name(events[0]->type, events[0]->code), Equals("MSC_SCAN"));
    REQUIRE(events[0]->value == 90005);

    REQUIRE_THAT(libevdev_event_type_get_name(events[1]->type), Equals("EV_KEY"));
    REQUIRE_THAT(libevdev_event_code_get_name(events[1]->type, events[1]->code), Equals("BTN_EXTRA"));
    REQUIRE(events[1]->value == 1);
  }

  SECTION("Mouse scroll") {
    short scroll_amt = 10;
    auto scroll_packet = pkts::MOUSE_SCROLL_PACKET{.scroll_amt1 = boost::endian::native_to_big(scroll_amt)};
    scroll_packet.type = pkts::MOUSE_SCROLL;

    control::handle_input(session, {}, &scroll_packet);
    events = fetch_events_debug(mouse_rel_dev);
    REQUIRE(events.size() == 1);
    REQUIRE_THAT(libevdev_event_type_get_name(events[0]->type), Equals("EV_REL"));
    REQUIRE_THAT(libevdev_event_code_get_name(events[0]->type, events[0]->code), Equals("REL_WHEEL_HI_RES"));
    REQUIRE(events[0]->value == scroll_amt);
  }

  SECTION("Mouse horizontal scroll") {
    short scroll_amt = 10;
    auto scroll_packet = pkts::MOUSE_HSCROLL_PACKET{.scroll_amount = boost::endian::native_to_big(scroll_amt)};
    scroll_packet.type = pkts::MOUSE_HSCROLL;

    control::handle_input(session, {}, &scroll_packet);
    events = fetch_events_debug(mouse_rel_dev);
    REQUIRE(events.size() == 1);
    REQUIRE_THAT(libevdev_event_type_get_name(events[0]->type), Equals("EV_REL"));
    REQUIRE_THAT(libevdev_event_code_get_name(events[0]->type, events[0]->code), Equals("REL_HWHEEL_HI_RES"));
    REQUIRE(events[0]->value == scroll_amt);
  }

  SECTION("UDEV") {
    auto udev_events = std::get<state::input::Mouse>(mouse->value()).get_udev_events();

    REQUIRE(udev_events.size() == 2);

    REQUIRE_THAT(udev_events[0]["ACTION"], Equals("add"));
    REQUIRE_THAT(udev_events[0]["ID_INPUT_MOUSE"], Equals("1"));
    REQUIRE_THAT(udev_events[0][".INPUT_CLASS"], Equals("mouse"));
    REQUIRE_THAT(udev_events[0]["DEVNAME"], ContainsSubstring("/dev/input/"));
    REQUIRE_THAT(udev_events[0]["DEVPATH"], StartsWith("/devices/virtual/input/input"));

    REQUIRE_THAT(udev_events[1]["ACTION"], Equals("add"));
    REQUIRE_THAT(udev_events[1]["ID_INPUT_TOUCHPAD"], Equals("1"));
    REQUIRE_THAT(udev_events[1][".INPUT_CLASS"], Equals("mouse"));
    REQUIRE_THAT(udev_events[1]["DEVNAME"], ContainsSubstring("/dev/input/"));
    REQUIRE_THAT(udev_events[1]["DEVPATH"], StartsWith("/devices/virtual/input/input"));
  }
}

TEST_CASE("uinput - joypad", "[UINPUT]") {
  SECTION("OLD Moonlight: create joypad on first packet arrival") {
    events::App app = {};
    auto session = events::StreamSession{.event_bus = std::make_shared<events::EventBusType>(),
                                         .app = std::make_shared<events::App>(app)};
    short controller_number = 1;
    auto c_pkt =
        pkts::CONTROLLER_MULTI_PACKET{.controller_number = controller_number, .button_flags = pkts::RIGHT_STICK};
    c_pkt.type = pkts::CONTROLLER_MULTI;

    control::handle_input(session, {}, &c_pkt);

    REQUIRE(session.joypads->load()->size() == 1);
    auto joypad = session.joypads->load()->at(controller_number);
    REQUIRE(joypad->get_nodes().size() == 2);
  }

  SECTION("NEW Moonlight: create joypad with CONTROLLER_ARRIVAL") {
    events::App app = {};
    auto session = events::StreamSession{.event_bus = std::make_shared<events::EventBusType>(),
                                         .app = std::make_shared<events::App>(app)};
    uint8_t controller_number = 1;
    auto c_pkt = pkts::CONTROLLER_ARRIVAL_PACKET{.controller_number = controller_number,
                                                 .controller_type = pkts::XBOX,
                                                 .capabilities = pkts::ANALOG_TRIGGERS};
    c_pkt.type = pkts::CONTROLLER_ARRIVAL;

    control::handle_input(session, {}, &c_pkt);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    auto joypad = session.joypads->load()->at(controller_number);
    std::vector<std::string> dev_nodes;
    dev_nodes = joypad->get_nodes();
    REQUIRE(session.joypads->load()->size() == 1);
    REQUIRE(dev_nodes.size() >= 2);

    // TODO: test pressing buttons

    { // UDEV
      std::vector<std::map<std::string, std::string>> udev_events;
      udev_events = joypad->get_udev_events();

      for (auto event : udev_events) {
        std::stringstream ss;
        for (auto [key, value] : event) {
          ss << key << "=" << value << ", ";
        }
        logs::log(logs::debug, "UDEV: {}", ss.str());
      }

      REQUIRE(udev_events.size() == 2);

      for (auto &event : udev_events) {
        REQUIRE_THAT(event["ACTION"], Equals("add"));
        REQUIRE_THAT(event["DEVNAME"], ContainsSubstring("/dev/input/"));
        REQUIRE_THAT(event["DEVPATH"], StartsWith("/devices/virtual/input/input"));
        REQUIRE_THAT(event[".INPUT_CLASS"], StartsWith("joystick"));
      }
    }
  }
}

TEST_CASE("uinput - paste UTF8", "[UINPUT]") {

  SECTION("UTF8 to HEX") {
    auto utf8 = boost::locale::conv::to_utf<wchar_t>("\xF0\x9F\x92\xA9", "UTF-8"); // UTF-8 '💩'
    auto utf32 = boost::locale::conv::utf_to_utf<char32_t>(utf8);
    REQUIRE_THAT(wolf::platforms::input::to_hex(utf32), Equals("1F4A9"));
  }

  SECTION("UTF16 to HEX") {
    char16_t payload[] = {0xD83D, 0xDCA9}; // UTF-16 '💩'
    auto utf16 = std::u16string(payload, 2);
    auto utf32 = boost::locale::conv::utf_to_utf<char32_t>(utf16);
    REQUIRE_THAT(wolf::platforms::input::to_hex(utf32), Equals("1F4A9"));
  }

  SECTION("Paste UTF8") {
    libevdev_ptr keyboard_dev(libevdev_new(), ::libevdev_free);
    auto session = events::StreamSession{
        .keyboard = std::make_shared<std::optional<events::KeyboardTypes>>(std::move(*Keyboard::create()))};
    link_devnode(keyboard_dev.get(), std::get<state::input::Keyboard>(session.keyboard->value()).get_nodes()[0]);

    auto events = fetch_events_debug(keyboard_dev);
    REQUIRE(events.empty());

    auto utf8_pkt = pkts::UTF8_TEXT_PACKET{.text = "\xF0\x9F\x92\xA9"};
    utf8_pkt.type = pkts::UTF8_TEXT;
    utf8_pkt.data_size = boost::endian::native_to_big(8);

    control::handle_input(session, {}, &utf8_pkt);
    events = fetch_events_debug(keyboard_dev);
    REQUIRE(events.size() == 16);

    /**
     * Lambda, checks that the given key_name has been correctly sent via evdev
     */
    auto ev_idx = 0;
    auto require_ev = [&](const std::string &key_name, bool pressed = true) {
      REQUIRE_THAT(libevdev_event_code_get_name(events[ev_idx]->type, events[ev_idx]->code), Equals(key_name));
      REQUIRE(events[ev_idx]->value == (pressed ? 1 : 0));
      ev_idx++;
    };

    /*
     * Pressing <CTRL> + <SHIFT> + U
     */
    require_ev("KEY_LEFTCTRL");
    require_ev("KEY_LEFTSHIFT");
    require_ev("KEY_U");
    require_ev("KEY_U", false); // release U

    /*
     * At this point we should have typed: U+1F4A9
     * (twice each because it's <press>, <release>
     */
    require_ev("KEY_1");
    require_ev("KEY_1", false);
    require_ev("KEY_F");
    require_ev("KEY_F", false);
    require_ev("KEY_4");
    require_ev("KEY_4", false);
    require_ev("KEY_A");
    require_ev("KEY_A", false);
    require_ev("KEY_9");
    require_ev("KEY_9", false);

    /*
     * Finally, releasing <CTRL> and <SHIFT>
     */
    require_ev("KEY_LEFTSHIFT", false);
    require_ev("KEY_LEFTCTRL", false);
  }
}

// ============================================================================
// Controller enumeration + reconnect regression tests.
//
// Drives control::handle_input directly with synthetic CONTROLLER_ARRIVAL
// packets (no client, no network) and inspects the real inputtino /dev/input
// joypads, including a disconnect/reconnect cycle (re-sent ARRIVAL — the path
// controller_arrival() takes when a Moonlight client resumes a session).
// Tag: [CONTROLLER].
// ============================================================================
namespace {

// The first /dev/input/eventNN node (used only as a stable per-pad id).
static std::string evdev_node(const std::vector<std::string> &nodes) {
  for (const auto &n : nodes) {
    if (n.find("/event") != std::string::npos) {
      return n;
    }
  }
  return nodes.empty() ? std::string{} : nodes[0];
}

// The /dev/input/eventNN node that actually exposes GAMEPAD BUTTONS. A uhid
// DualSense / Switch pad also exposes separate motion + touchpad event nodes,
// so "first event node" is not necessarily the pad — this is what SDL/gilrs
// (the launcher) treats as "a controller". Returns "" if no such node exists,
// which would mean the gamepad failed to enumerate.
static std::string find_gamepad_node(const std::vector<std::string> &nodes) {
  for (const auto &n : nodes) {
    if (n.find("/event") == std::string::npos) {
      continue;
    }
    int fd = open(n.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
      continue;
    }
    libevdev_ptr dev(libevdev_new(), ::libevdev_free);
    bool is_pad = false;
    if (libevdev_set_fd(dev.get(), fd) == 0) {
      is_pad = libevdev_has_event_code(dev.get(), EV_KEY, BTN_GAMEPAD) ||
               libevdev_has_event_code(dev.get(), EV_KEY, BTN_SOUTH) ||
               libevdev_has_event_code(dev.get(), EV_KEY, BTN_A);
    }
    close(fd);
    if (is_pad) {
      return n;
    }
  }
  return {};
}

struct JoypadProbe {
  std::string name;
  int vendor = 0;
  int product = 0;
  bool has_keys = false;
  bool has_abs = false;
};

// Open the joypad's GAMEPAD evdev node and read identity + that it's a real gamepad.
static JoypadProbe probe_joypad(const std::shared_ptr<events::JoypadTypes> &pad) {
  auto node = find_gamepad_node(pad->get_nodes());
  REQUIRE(!node.empty()); // a gamepad-buttons node must have enumerated
  libevdev_ptr dev(libevdev_new(), ::libevdev_free);
  link_devnode(dev.get(), node); // REQUIREs fd >= 0 internally
  JoypadProbe p;
  const char *nm = libevdev_get_name(dev.get());
  p.name = nm ? nm : "";
  p.vendor = libevdev_get_id_vendor(dev.get());
  p.product = libevdev_get_id_product(dev.get());
  p.has_keys = libevdev_has_event_type(dev.get(), EV_KEY);
  p.has_abs = libevdev_has_event_type(dev.get(), EV_ABS);
  return p;
}

static pkts::CONTROLLER_ARRIVAL_PACKET make_arrival(uint8_t slot, pkts::CONTROLLER_TYPE type, uint8_t caps) {
  pkts::CONTROLLER_ARRIVAL_PACKET pkt{.controller_number = slot, .controller_type = type, .capabilities = caps};
  pkt.type = pkts::CONTROLLER_ARRIVAL;
  return pkt;
}

static events::StreamSession make_session(const wolf::config::ClientSettings &cs = {}, bool use_uhid = true) {
  events::App app = {};
  app.use_uhid = use_uhid;
  return events::StreamSession{.event_bus = std::make_shared<events::EventBusType>(),
                               .client_settings = immer::box<wolf::config::ClientSettings>(cs),
                               .app = std::make_shared<events::App>(app)};
}

static void settle() {
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
}

constexpr int VID_XBOX = 0x045E;
constexpr int VID_PS = 0x054C;
constexpr int VID_NINTENDO = 0x057e;

} // namespace

TEST_CASE("controller: single, no override, no gyro", "[CONTROLLER]") {
  auto session = make_session();
  auto pkt = make_arrival(0, pkts::XBOX, pkts::ANALOG_TRIGGERS);

  control::handle_input(session, {}, &pkt);
  settle();

  REQUIRE(session.joypads->load()->size() == 1);
  auto pad = session.joypads->load()->at(0);
  REQUIRE(pad->get_nodes().size() >= 2);
  REQUIRE_FALSE(pad->supports_motion()); // Xbox/uinput: no motion
  auto p1 = probe_joypad(pad);
  REQUIRE(p1.vendor == VID_XBOX);
  REQUIRE(p1.has_keys);
  REQUIRE(p1.has_abs);
  auto node_before = evdev_node(pad->get_nodes());

  { // udev add events look sane
    auto udev = pad->get_udev_events();
    REQUIRE(udev.size() == 2);
    for (auto &e : udev) {
      REQUIRE_THAT(e["ACTION"], Equals("add"));
      REQUIRE_THAT(e["DEVNAME"], ContainsSubstring("/dev/input/"));
      REQUIRE_THAT(e[".INPUT_CLASS"], StartsWith("joystick"));
    }
  }

  // RECONNECT: client re-sends ARRIVAL on the same slot.
  control::handle_input(session, {}, &pkt);
  settle();

  REQUIRE(session.joypads->load()->size() == 1); // no duplicate / leak
  auto pad2 = session.joypads->load()->at(0);
  auto p2 = probe_joypad(pad2); // still a functional gamepad
  REQUIRE(p2.vendor == VID_XBOX);
  REQUIRE(p2.has_keys);
  REQUIRE(p2.has_abs);
  auto node_after = evdev_node(pad2->get_nodes());
  INFO("evdev node before reconnect: " << node_before << "  after: " << node_after);
  SUCCEED("reconnect kept exactly one functional Xbox pad");
}

TEST_CASE("controller: overrides + gyro", "[CONTROLLER]") {
  const bool uhid = inputtino::is_uhid_supported();

  SECTION("controllers_override forces PS over advertised XBOX") {
    wolf::config::ClientSettings cs;
    cs.controllers_override = {wolf::config::ControllerType::PS};
    auto session = make_session(cs);
    auto pkt = make_arrival(0, pkts::XBOX, pkts::ANALOG_TRIGGERS);
    control::handle_input(session, {}, &pkt);
    settle();
    REQUIRE(probe_joypad(session.joypads->load()->at(0)).vendor == VID_PS);
  }

  SECTION("motion_controller_override promotes a gyro UNKNOWN client to PS w/ motion") {
    wolf::config::ClientSettings cs;
    cs.motion_controller_override = wolf::config::ControllerType::PS;
    auto session = make_session(cs);
    auto pkt = make_arrival(0, pkts::UNKNOWN, pkts::GYRO | pkts::ACCELEROMETER);
    control::handle_input(session, {}, &pkt);
    settle();
    auto pad = session.joypads->load()->at(0);
    REQUIRE(probe_joypad(pad).vendor == VID_PS);
    if (uhid) {
      REQUIRE(pad->supports_motion());
    }
  }

  SECTION("advertised PS + gyro stays PS w/ motion across reconnect") {
    auto session = make_session(); // AUTO, no overrides
    auto pkt = make_arrival(0, pkts::PS, pkts::GYRO | pkts::ACCELEROMETER | pkts::ANALOG_TRIGGERS);
    control::handle_input(session, {}, &pkt);
    settle();
    auto pad = session.joypads->load()->at(0);
    REQUIRE(probe_joypad(pad).vendor == VID_PS);
    if (uhid) {
      REQUIRE(pad->supports_motion());
    }

    control::handle_input(session, {}, &pkt); // reconnect
    settle();
    REQUIRE(session.joypads->load()->size() == 1);
    auto pad2 = session.joypads->load()->at(0);
    REQUIRE(probe_joypad(pad2).vendor == VID_PS);
    if (uhid) {
      REQUIRE(pad2->supports_motion());
    }
  }
}

TEST_CASE("controller: three controllers + reconnect", "[CONTROLLER]") {
  auto session = make_session();
  std::vector<std::pair<uint8_t, pkts::CONTROLLER_TYPE>> pads = {
      {0, pkts::XBOX}, {1, pkts::NINTENDO}, {2, pkts::XBOX}};
  std::vector<int> want_vendor = {VID_XBOX, VID_NINTENDO, VID_XBOX};

  auto inject_all = [&]() {
    for (auto &[slot, type] : pads) {
      auto pkt = make_arrival(slot, type, pkts::ANALOG_TRIGGERS);
      control::handle_input(session, {}, &pkt);
    }
    settle();
  };

  auto verify_all = [&]() {
    auto list = session.joypads->load();
    REQUIRE(list->size() == 3);
    std::set<std::string> nodes;
    for (size_t i = 0; i < pads.size(); i++) {
      auto pad = list->at(pads[i].first);
      auto p = probe_joypad(pad);
      REQUIRE(p.vendor == want_vendor[i]);
      REQUIRE(p.has_keys);
      nodes.insert(evdev_node(pad->get_nodes()));
    }
    REQUIRE(nodes.size() == 3); // distinct /dev nodes
  };

  inject_all();
  verify_all();

  // RECONNECT: re-inject all three.
  inject_all();
  verify_all(); // still exactly 3, all functional, distinct
  SUCCEED("three controllers re-enumerated cleanly after reconnect");
}

TEST_CASE("controller: inputtino recreate_device primitive", "[CONTROLLER]") {
  const bool uhid = inputtino::is_uhid_supported();

  auto check_recreate = [](std::shared_ptr<events::JoypadTypes> pad) {
    settle();
    auto before = pad->get_nodes();
    REQUIRE(before.size() >= 2);
    REQUIRE_FALSE(find_gamepad_node(before).empty()); // gamepad present pre-recreate
    pad->recreate_device();
    settle();
    auto after = pad->get_nodes();
    REQUIRE(after.size() >= 2);
    // The recreated device must STILL expose a functional gamepad node.
    auto node = find_gamepad_node(after);
    REQUIRE_FALSE(node.empty());
    libevdev_ptr dev(libevdev_new(), ::libevdev_free);
    link_devnode(dev.get(), node);
    REQUIRE(libevdev_has_event_type(dev.get(), EV_KEY));
  };

  SECTION("Xbox (uinput)") {
    auto pad = std::shared_ptr<events::JoypadTypes>(std::move(*inputtino::Joypad::create(inputtino::JoypadKind::XBOX)));
    check_recreate(pad);
  }
  SECTION("PS5 (uhid)") {
    if (!uhid) {
      SKIP("/dev/uhid not usable in this environment");
    }
    auto pad = std::shared_ptr<events::JoypadTypes>(std::move(*inputtino::Joypad::create(inputtino::JoypadKind::PS)));
    REQUIRE(pad->supports_motion());
    check_recreate(pad);
  }
  SECTION("Switch (uhid)") {
    if (!uhid) {
      SKIP("/dev/uhid not usable in this environment");
    }
    auto pad =
        std::shared_ptr<events::JoypadTypes>(std::move(*inputtino::Joypad::create(inputtino::JoypadKind::NINTENDO)));
    check_recreate(pad);
  }
}

// ============================================================================
// Layer 2: container-udev / FD-severing (issue #435) + departure corner cases.
// ============================================================================
namespace {

static libevdev_ptr open_evdev_fd(const std::string &node, int &out_fd) {
  out_fd = open(node.c_str(), O_RDONLY | O_NONBLOCK);
  REQUIRE(out_fd >= 0);
  libevdev *raw = nullptr;
  REQUIRE(libevdev_new_from_fd(out_fd, &raw) == 0);
  return libevdev_ptr(raw, ::libevdev_free);
}

static void drain_evdev(const libevdev_ptr &dev) {
  struct input_event ev;
  while (libevdev_next_event(dev.get(), LIBEVDEV_READ_FLAG_NORMAL, &ev) == LIBEVDEV_READ_STATUS_SUCCESS) {
  }
}

// Poll ~400ms for a button (EV_KEY) event on this fd.
static bool saw_button(const libevdev_ptr &dev) {
  for (int i = 0; i < 40; i++) {
    struct input_event ev;
    int rc = libevdev_next_event(dev.get(), LIBEVDEV_READ_FLAG_NORMAL, &ev);
    if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
      if (ev.type == EV_KEY) {
        return true;
      }
    } else if (rc == -ENODEV) {
      return false;
    } else { // -EAGAIN
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
  return false;
}

// Is the device behind this fd destroyed (UI_DEV_DESTROY)? Polls for -ENODEV.
static bool fd_severed(const libevdev_ptr &dev) {
  for (int i = 0; i < 60; i++) {
    struct input_event ev;
    int rc = libevdev_next_event(dev.get(), LIBEVDEV_READ_FLAG_NORMAL, &ev);
    if (rc == -ENODEV) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

// DEVNAMEs of the joystick-class nodes in a udev batch. (The ACTION field in the
// maps is always "add" — the device's intrinsic udev representation; whether the
// container adds or removes is conveyed by the EVENT TYPE, Plug vs Unplug.)
static std::vector<std::string> joystick_udev(const std::vector<std::map<std::string, std::string>> &udev) {
  std::vector<std::string> out;
  for (const auto &e : udev) {
    auto cls = e.find(".INPUT_CLASS");
    auto dev = e.find("DEVNAME");
    if (cls != e.end() && cls->second.rfind("joystick", 0) == 0 && dev != e.end()) {
      out.push_back(dev->second);
    }
  }
  return out;
}

} // namespace

TEST_CASE("controller: #435 recreate_device severs the stale source FD (no input leak)", "[CONTROLLER]") {
  auto pad = std::shared_ptr<events::JoypadTypes>(std::move(*inputtino::Joypad::create(inputtino::JoypadKind::XBOX)));
  settle();
  auto node_before = find_gamepad_node(pad->get_nodes());
  REQUIRE_FALSE(node_before.empty());

  // "Source container" (e.g. the Wolf-UI Godot process) opens an fd on the shared node.
  int src_fd = -1;
  auto src = open_evdev_fd(node_before, src_fd);

  // Live: a button reaches the source fd.
  drain_evdev(src);
  pad->set_pressed_buttons(inputtino::Joypad::A);
  REQUIRE(saw_button(src));

  // The migrate primitive: destroy + re-create the device in place.
  pad->recreate_device();
  settle();

  // #435 ASSERT 1: the source's still-open fd is SEVERED -> input can no longer leak into it.
  pad->set_pressed_buttons(inputtino::Joypad::B); // would-be leaked input (goes to the NEW device)
  REQUIRE(fd_severed(src));
  close(src_fd);

  // ASSERT 2: the freshly created node is a functional gamepad (the target/game device).
  auto node_after = find_gamepad_node(pad->get_nodes());
  REQUIRE_FALSE(node_after.empty());
  INFO("source node: " << node_before << "  recreated node: " << node_after);
  int tgt_fd = -1;
  auto tgt = open_evdev_fd(node_after, tgt_fd);
  drain_evdev(tgt);
  pad->set_pressed_buttons(inputtino::Joypad::A);
  REQUIRE(saw_button(tgt));
  close(tgt_fd);
}

TEST_CASE("controller: migrate re-enumerates the gamepad in the target container udev view", "[CONTROLLER]") {
  auto session = make_session();

  std::vector<std::string> plugged, unplugged; // joystick-node DEVNAMEs added / removed
  auto reg_plug = session.event_bus->register_handler<immer::box<events::PlugDeviceEvent>>(
      [&](const immer::box<events::PlugDeviceEvent> &ev) {
        auto v = joystick_udev(ev->udev_events); // PlugDeviceEvent => the container ADDs these
        plugged.insert(plugged.end(), v.begin(), v.end());
      });
  auto reg_unplug = session.event_bus->register_handler<immer::box<events::UnplugDeviceEvent>>(
      [&](const immer::box<events::UnplugDeviceEvent> &ev) {
        auto v = joystick_udev(ev->udev_events); // UnplugDeviceEvent => the container REMOVEs these
        unplugged.insert(unplugged.end(), v.begin(), v.end());
      });

  // ARRIVAL -> create_new_joypad fires PlugDeviceEvent (gamepad enumerated in the source container).
  auto pkt = make_arrival(0, pkts::XBOX, pkts::ANALOG_TRIGGERS);
  control::handle_input(session, {}, &pkt);
  settle();
  REQUIRE_FALSE(plugged.empty()); // gamepad "add" reached the container view

  // Replicate migrate_joypad's 3 steps (lobbies.cpp, file-local): unplug source, recreate, plug target.
  auto pad = session.joypads->load()->at(0);
  plugged.clear();
  unplugged.clear();

  events::UnplugDeviceEvent un{.session_id = "source"};
  un.udev_events = pad->get_udev_events();
  un.udev_hw_db_entries = pad->get_udev_hw_db_entries();
  session.event_bus->fire_event(immer::box<events::UnplugDeviceEvent>(un));

  pad->recreate_device();
  settle();

  events::PlugDeviceEvent pl{.session_id = "target"};
  pl.udev_events = pad->get_udev_events();
  pl.udev_hw_db_entries = pad->get_udev_hw_db_entries();
  session.event_bus->fire_event(immer::box<events::PlugDeviceEvent>(pl));

  INFO("removed: " << (unplugged.empty() ? "<none>" : unplugged.back())
                   << "  added: " << (plugged.empty() ? "<none>" : plugged.back()));
  REQUIRE_FALSE(unplugged.empty()); // old gamepad node removed from source view
  REQUIRE_FALSE(plugged.empty());   // NEW gamepad node added to target view -> the app re-enumerates it
}

TEST_CASE("controller: departure (active_gamepad_mask) unplugs + erases", "[CONTROLLER]") {
  auto session = make_session();
  auto arr = make_arrival(0, pkts::XBOX, pkts::ANALOG_TRIGGERS);
  control::handle_input(session, {}, &arr);
  settle();
  REQUIRE(session.joypads->load()->size() == 1);
  auto node = find_gamepad_node(session.joypads->load()->at(0)->get_nodes());
  REQUIRE_FALSE(node.empty());

  // CONTROLLER_MULTI with this slot's bit CLEARED -> wolf unplugs + erases.
  pkts::CONTROLLER_MULTI_PACKET multi{.controller_number = 0, .active_gamepad_mask = 0};
  multi.type = pkts::CONTROLLER_MULTI;
  control::handle_input(session, {}, &multi);
  settle();

  REQUIRE(session.joypads->load()->size() == 0); // pad erased
  bool gone = false;                             // and the /dev node removed
  for (int i = 0; i < 30 && !gone; i++) {
    int fd = open(node.c_str(), O_RDONLY);
    if (fd < 0) {
      gone = true;
    } else {
      close(fd);
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }
  REQUIRE(gone);
}

TEST_CASE("controller: slot reuse with a different type", "[CONTROLLER]") {
  auto session = make_session();
  auto a1 = make_arrival(0, pkts::XBOX, pkts::ANALOG_TRIGGERS);
  control::handle_input(session, {}, &a1);
  settle();
  REQUIRE(probe_joypad(session.joypads->load()->at(0)).vendor == VID_XBOX);

  pkts::CONTROLLER_MULTI_PACKET multi{.controller_number = 0, .active_gamepad_mask = 0};
  multi.type = pkts::CONTROLLER_MULTI;
  control::handle_input(session, {}, &multi);
  settle();
  REQUIRE(session.joypads->load()->size() == 0);

  auto a2 = make_arrival(0, pkts::NINTENDO, pkts::ANALOG_TRIGGERS);
  control::handle_input(session, {}, &a2);
  settle();
  REQUIRE(session.joypads->load()->size() == 1);
  auto p = probe_joypad(session.joypads->load()->at(0));
  REQUIRE(p.vendor == VID_NINTENDO); // slot reused with the new type
  REQUIRE_FALSE(find_gamepad_node(session.joypads->load()->at(0)->get_nodes()).empty());
}

TEST_CASE("controller: four controllers (max) then remove one", "[CONTROLLER]") {
  auto session = make_session();
  std::vector<pkts::CONTROLLER_TYPE> types = {pkts::XBOX, pkts::PS, pkts::NINTENDO, pkts::XBOX};
  for (uint8_t s = 0; s < 4; s++) {
    auto a = make_arrival(s, types[s], pkts::ANALOG_TRIGGERS);
    control::handle_input(session, {}, &a);
  }
  settle();
  REQUIRE(session.joypads->load()->size() == 4);
  std::set<std::string> nodes;
  for (int s = 0; s < 4; s++) {
    auto n = find_gamepad_node(session.joypads->load()->at(s)->get_nodes());
    REQUIRE_FALSE(n.empty());
    nodes.insert(n);
  }
  REQUIRE(nodes.size() == 4); // four distinct gamepad nodes

  // Remove slot 1; active mask keeps 0,2,3 (0b1101).
  pkts::CONTROLLER_MULTI_PACKET multi{.controller_number = 1, .active_gamepad_mask = 0b1101};
  multi.type = pkts::CONTROLLER_MULTI;
  control::handle_input(session, {}, &multi);
  settle();
  auto list = session.joypads->load();
  REQUIRE(list->size() == 3);
  REQUIRE(list->find(1) == nullptr); // slot 1 gone
  REQUIRE(list->find(0) != nullptr);
  REQUIRE(list->find(2) != nullptr);
  REQUIRE(list->find(3) != nullptr);
}
