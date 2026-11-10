include <params.scad>
use <lib.scad>
use <dummies.scad>
use <top.scad>
use <body.scad>
use <ring.scad>
use <tray.scad>
use <small.scad>

case_color = "#3c3c3c";
tpu_color = "#262626";
arc_color = "white";

color(case_color) {
    body();
    top();
    tray();
    button_cap();
    top_plugs();
}
color(tpu_color) {
    dummy_feet();
    com_bushing();
}
color(arc_color) ring_arc();
