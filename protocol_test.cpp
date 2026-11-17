// SPDX-License-Identifier: GPL-3.0-only
#include "protocol.pb.h"
#include <iostream>
#include <stdexcept>

void check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        selectorwire::GlobalSettings original;
        auto *unknown = original.GetReflection()->MutableUnknownFields(&original);
        unknown->AddVarint(22, 1); // Audio spectrum capture setting.
        unknown->AddVarint(12, 0); // Rotation interval.
        unknown->AddLengthDelimited(11, "sequential");
        unknown->AddLengthDelimited(20, std::string("\x08\x00\x10\x64", 4)); // Opaque renderer options.
        unknown->AddLengthDelimited(31, std::string("\x08\x03", 2)); // 0.6 hidden-library visibility.
        original.mutable_transition()->set_duration_ms(500);
        auto changed = original;
        changed.mutable_transition()->set_kind(1);
        changed.mutable_transition()->set_origin_x(50);
        changed.mutable_transition()->set_origin_y(50);
        std::string before, after;
        check(original.GetReflection()->GetUnknownFields(original).SerializeToString(&before), "Could not serialize original settings");
        check(changed.GetReflection()->GetUnknownFields(changed).SerializeToString(&after), "Could not serialize preserved settings");
        check(before == after, "A non-transition setting changed");

        selectorwire::SettingsGetResponse get;
        get.set_global(original.SerializeAsString());
        get.add_plugins("opaque-video-plugin-settings");
        get.add_plugins("opaque-scene-plugin-settings");
        selectorwire::SettingsSetRequest set;
        set.set_global(changed.SerializeAsString());
        for (const auto &entry : get.plugins()) set.add_plugins(entry);
        check(set.plugins(0) == get.plugins(0) && set.plugins(1) == get.plugins(1), "Plugin settings changed");

        selectorwire::Request apply;
        apply.set_request_id(42);
        apply.mutable_wallpaper_apply()->set_wallpaper_id("123");
        apply.mutable_wallpaper_apply()->add_targets()->set_display_id(7);
        selectorwire::Request decoded;
        check(decoded.ParseFromString(apply.SerializeAsString()), "Could not parse application request");
        check(decoded.wallpaper_apply().targets_size() == 1, "Apply must have exactly one explicit destination");
        check(decoded.wallpaper_apply().targets(0).display_id() == 7, "Wrong monitor destination");
        std::cout << "Preserved global/plugin settings and verified a single explicit monitor target.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
