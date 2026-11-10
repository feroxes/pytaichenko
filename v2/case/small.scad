include <params.scad>
use <lib.scad>

small_fn = 96;
gasket_fn = 360;

bcap_stem_r = (button_hole_d - 2 * clr) / 2;
bcap_flange_r = btn_flange_d / 2;
bcap_socket_r = (btn_pusher_d + 2 * clr) / 2;
bcap_flange_h = 1.5;
bcap_cone_h = 1.0;
bcap_top_chamfer = 0.4;
bcap_z0 = btn_tip_z - bcap_flange_h;
bcap_straight_h = bcap_flange_h - bcap_cone_h;

foot_chamfer_r = 0.6;
foot_chamfer_h = 0.8;

plug_fn = 64;
plug_asm_d = top_plug_d - 0.01;

module bcap_local() {
    difference() {
        union() {
            translate([0, 0, bcap_z0])
                cylinder(r = bcap_flange_r, h = bcap_straight_h, $fn = small_fn);
            translate([0, 0, bcap_z0 + bcap_straight_h])
                cylinder(r1 = bcap_flange_r, r2 = bcap_stem_r, h = bcap_cone_h, $fn = small_fn);
            translate([0, 0, btn_tip_z])
                cylinder(r = bcap_stem_r, h = H - bcap_top_chamfer - btn_tip_z, $fn = small_fn);
            translate([0, 0, H - bcap_top_chamfer])
                cylinder(r1 = bcap_stem_r, r2 = bcap_stem_r - bcap_top_chamfer, h = bcap_top_chamfer, $fn = small_fn);
        }
        translate([0, 0, bcap_z0 - 1])
            cylinder(r = bcap_socket_r, h = btn_tip_z - bcap_z0 + 1, $fn = small_fn);
    }
}

module button_cap() {
    rotate([0, 0, button_az]) translate([button_r, 0, 0]) bcap_local();
}

module print_button_cap() {
    translate([0, 0, H]) rotate([180, 0, 0]) bcap_local();
}

module foot() {
    hull() {
        linear_extrude(0.01) offset(delta = -foot_chamfer_r) foot2d();
        translate([0, 0, foot_chamfer_h]) linear_extrude(foot_h - foot_chamfer_h) foot2d();
    }
}

module top_plug_envelope(d) {
    c = top_plug_chamfer;
    e = top_plug_edge;
    h = top_plug_h;
    rotate_extrude($fn = plug_fn)
        polygon([[0, 0], [d / 2 - e, 0], [d / 2, e], [d / 2, h - c], [d / 2 - c, h], [0, h]]);
}

module top_plug_local(d) {
    top_plug_envelope(d);
}

module print_top_plug() {
    intersection() {
        top_plug_envelope(top_plug_d + top_plug_fit);
        union() {
            cylinder(d = top_plug_d - 2 * top_plug_clr, h = top_plug_h, $fn = plug_fn);
            for (k = [0 : top_plug_ribs - 1]) rotate(k * 360 / top_plug_ribs)
                translate([0, -top_plug_rib_w / 2, 0]) cube([top_plug_d, top_plug_rib_w, top_plug_h]);
        }
    }
}

module top_plugs() {
    for (p = top_screw_xy) translate([p[0], p[1], H]) mirror([0, 0, 1]) top_plug_local(plug_asm_d);
}

module speaker_gasket() {
    translate([0, 0, spk_gasket_z0]) ring_band(spk_gasket_od / 2 - 0.01, spk_gasket_od / 2 - spk_gasket_id / 2 - 0.02,
        top_under_z - spk_gasket_z0, gasket_fn);
}

module print_speaker_gasket() {
    ring_band(spk_gasket_od / 2, spk_gasket_od / 2 - spk_gasket_id / 2, spk_gasket_t, gasket_fn);
}

module print_top_gasket() {
    ring_band(top_gasket_od / 2, top_gasket_w, top_gasket_t, gasket_fn);
}

module print_tray_gasket() {
    ring_band(tray_gasket_od / 2, tray_gasket_w, tray_gasket_t, gasket_fn);
}

module com_bushing_local(grow) {
    intersection() {
        difference() {
            box(dk_com_x - (tray_com_w + grow) / 2, dk_com_x + (tray_com_w + grow) / 2, -case_r - 2, com_bush_y1,
                usb_z - (tray_com_h + grow) / 2, usb_z + (tray_com_h + grow) / 2);
            box(dk_com_x - com_hole_w / 2, dk_com_x + com_hole_w / 2, -case_r - 3, com_bush_y1 + 1,
                usb_z - com_hole_h / 2, usb_z + com_hole_h / 2);
        }
        cylinder(r = case_r, h = 100, $fn = 360);
    }
}

module com_bushing() {
    com_bushing_local(-0.01);
}

module print_com_bushing() {
    rotate([-90, 0, 0]) translate([-dk_com_x, -com_bush_y1, -usb_z]) com_bushing_local(com_bush_fit);
}

module echo_small() {
    echo(bcap_stem_r=bcap_stem_r, bcap_flange_r=bcap_flange_r, bcap_socket_r=bcap_socket_r, bcap_flange_h=bcap_flange_h, bcap_cone_h=bcap_cone_h, bcap_top_chamfer=bcap_top_chamfer, bcap_z0=bcap_z0);
    echo(foot_chamfer_r=foot_chamfer_r, foot_chamfer_h=foot_chamfer_h, plug_asm_d=plug_asm_d);
}
