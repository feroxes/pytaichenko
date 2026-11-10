include <params.scad>
use <lib.scad>
use <dummies.scad>
use <top.scad>
use <body.scad>
use <ring.scad>
use <tray.scad>
use <small.scad>

step = 1;

new_color = "darkorange";
old_color = "lightslategray";

STEPS = [
    [1, "tray", [], [0, 0, 0]],
    [1, "tray", [], [0, 0, 0]],
    [1, "tray", [], [0, 0, 0]],
    [1, "tray", [], [0, 0, 0]],
    [2, "body_flip", [], [0, 0, 0]],
    [2, "body", [], [0, 0, 0]],
    [2, "body", [], [0, 0, 0]],
    [2, "body", [], [0, 0, 0]],
    [2, "body", [], [0, 0, 0]],
    [2, "body", [], [0, 0, 0]],
    [2, "body", [], [0, 0, 0]],
    [2, "base", ["body"], [0, 0, 34]],
    [2, "base_flip", [], [0, 0, 0]],
    [2, "base_flip", [], [0, 0, 0]],
    [2, "base", [], [0, 0, 0]],
    [3, "lid_flip", [], [0, 0, 0]],
    [3, "lid_flip", [], [0, 0, 0]],
    [3, "lid_flip", [], [0, 0, 0]],
    [3, "lid_flip", [], [0, 0, 0]],
    [3, "lid_flip", [], [0, 0, 0]],
    [3, "lid_flip", [], [0, 0, 0]],
    [3, "lid_flip", [], [0, 0, 0]],
    [3, "lid_flip", [], [0, 0, 0]],
    [3, "all", ["lid"], [0, 0, 26]],
    [3, "all", [], [0, 0, 0]],
    [3, "all", [], [0, 0, 0]]
];

PARTS = [
    ["body", 0, "body", [0, 0, 0]],
    ["top", 0, "lid", [0, 0, 0]],
    ["tray", 0, "tray", [0, 0, 0]],
    ["tray_inserts", 5, "body", [0, 0, -14]],
    ["column_inserts", 6, "body", [0, 0, 14]],
    ["mic_l", 7, "body", concat(-12 * polar(1, mic_az_l), [0])],
    ["mic_r", 8, "body", concat(-12 * polar(1, mic_az_r), [0])],
    ["glue_mics", 9, "body", [0, 0, 0]],
    ["filament", 10, "body", [0, 0, 14]],
    ["filament_tails", 10, "body", [0, 0, 14]],
    ["ring_arc", 11, "body", [0, 0, 14]],
    ["spk_inserts", 16, "lid", [0, 0, -12]],
    ["button_cap", 17, "lid", [0, 0, -14]],
    ["button", 18, "lid", [0, 0, -14]],
    ["glue_switch", 19, "lid", [0, 0, 0]],
    ["speaker_gasket", 20, "lid", [0, 0, -12]],
    ["speaker", 21, "lid", [0, 0, -30]],
    ["metal_washers", 22, "lid", [0, 0, -12]],
    ["speaker_screws", 22, "lid", [0, 0, -24]],
    ["top_gasket", 23, "lid", [0, 0, -12]],
    ["top_screws", 25, "lid", [0, 0, 16]],
    ["top_plugs", 26, "lid", [0, 0, 16]],
    ["devkit", 1, "tray", [0, 0, 16]],
    ["antenna", 1, "tray", [0, 0, 16]],
    ["amp", 2, "tray", [0, 0, 10]],
    ["cap", 2, "tray", [0, 0, 10]],
    ["transistor", 3, "tray", [0, 0, 14]],
    ["glue_transistor", 3, "tray", [0, 0, 0]],
    ["tray_gasket", 4, "tray", [0, 0, 10]],
    ["tray_screws", 13, "tray", [0, 0, -20]],
    ["feet", 14, "tray", [0, 0, -12]],
    ["com_bushing", 15, "tray", [0, -14, 0]],
    ["com_plug", 15, "tray", [0, -24, 0]]
];

VIEWS = [
    ["body", ["body"], false],
    ["body_flip", ["body"], true],
    ["lid_flip", ["lid"], true],
    ["tray", ["tray"], false],
    ["base", ["body", "tray"], false],
    ["base_flip", ["body", "tray"], true],
    ["all", ["body", "lid", "tray"], false]
];

function has(list, x) = len([for (e = list) if (e == x) e]) > 0;
function view_of(name) = [for (v = VIEWS) if (v[0] == name) v][0];
function parts_at(n) = [for (p = PARTS) if (p[1] <= n) p[0]];
function new_at(n) = [for (p = PARTS) if (p[1] == n) p[0]];

module glue_mics() {
    for (az = [mic_az_l, mic_az_r])
        rotate([0, 0, az]) translate([mic_face_r - mic_t - 0.5, 0, mic_z]) rotate([0, 90, 0])
            rotate_extrude($fn = 48) translate([mic_d / 2, 0]) scale([1, 1.4]) circle(d = 1.6, $fn = 16);
}

module glue_switch() {
    rotate([0, 0, button_az]) translate([button_r, 0, 0])
        for (s = [-1, 1]) translate([0, s * btn_w / 2, btn_z0]) scale([1.4, 1, 1]) sphere(d = 3.6, $fn = 24);
}

module glue_transistor() {
    translate([transistor_xy[0], transistor_xy[1] - 2, tray_floor_t - 0.01])
        scale([1, 1.5, 1]) cylinder(d = tr_w + 3, h = 0.8, $fn = 48);
}

module draw(name) {
    if (name == "body") body();
    else if (name == "top") top();
    else if (name == "tray") tray();
    else if (name == "tray_inserts") dummy_tray_inserts();
    else if (name == "column_inserts") dummy_column_inserts();
    else if (name == "mic_l") dummy_mic(mic_az_l);
    else if (name == "mic_r") dummy_mic(mic_az_r);
    else if (name == "glue_mics") glue_mics();
    else if (name == "filament") dummy_filament();
    else if (name == "filament_tails") dummy_filament_tails();
    else if (name == "ring_arc") ring_arc();
    else if (name == "spk_inserts") dummy_spk_inserts();
    else if (name == "button_cap") button_cap();
    else if (name == "button") dummy_button();
    else if (name == "glue_switch") glue_switch();
    else if (name == "speaker_gasket") speaker_gasket();
    else if (name == "speaker") dummy_speaker();
    else if (name == "metal_washers") dummy_metal_washers();
    else if (name == "speaker_screws") dummy_speaker_screws();
    else if (name == "top_gasket") dummy_top_gasket();
    else if (name == "top_screws") dummy_top_screws();
    else if (name == "top_plugs") top_plugs();
    else if (name == "devkit") dummy_devkit();
    else if (name == "antenna") dummy_antenna();
    else if (name == "amp") dummy_amp();
    else if (name == "cap") dummy_cap();
    else if (name == "transistor") dummy_transistor();
    else if (name == "glue_transistor") glue_transistor();
    else if (name == "tray_gasket") dummy_tray_gasket();
    else if (name == "tray_screws") dummy_tray_screws();
    else if (name == "feet") dummy_feet();
    else if (name == "com_bushing") com_bushing();
    else if (name == "com_plug") dummy_usb_plug(com_plug_w, com_plug_h);
    else assert(false, str("unknown part: ", name));
}

module flip_if(on, h) {
    if (on) translate([0, 0, h]) rotate([0, 180, 0]) children();
    else children();
}

module scene(n) {
    s = STEPS[n - 1];
    v = view_of(s[1]);
    h = s[1] == "body_flip" ? top_z0 : H;
    flip_if(v[2], h) for (p = PARTS) if (p[1] <= n && has(v[1], p[2])) {
        moving = has(s[2], p[2]);
        fresh = p[1] == n;
        translate(moving ? s[3] : fresh ? p[3] : [0, 0, 0])
            color(moving || fresh ? new_color : old_color) draw(p[0]);
    }
}

assert(step >= 1 && step <= len(STEPS), str("unknown step: ", step));
echo(steps = len(STEPS));
echo(step = step);
echo(stage = STEPS[step - 1][0]);
echo(parts = parts_at(step));
echo(new = new_at(step));
echo(moving = STEPS[step - 1][2]);
scene(step);
