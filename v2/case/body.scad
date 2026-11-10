include <params.scad>
use <lib.scad>

shell_fn = 360;
slot_fn = 16;

module tube() {
    rotate_extrude($fn = shell_fn) translate([inner_r, body_bot_z]) square([wall_t, top_z0 - body_bot_z]);
}

module lit_cut() {
    rotate([0, 0, 90 - fil_ring_half - ring_end_deg])
        rotate_extrude(angle = fil_ring_deg + 2 * ring_end_deg, $fn = shell_fn)
            translate([inner_r - 1, ring_z0]) square([wall_t + 2, top_z0 - ring_z0 + 1]);
}

module tail_slot(pts) {
    for (i = [0 : len(pts) - 2]) hull() {
        for (q = [pts[i], pts[i + 1]], dz = [0, top_z0 + 1 - fil_z])
            translate(q + [0, 0, dz]) sphere(d = chan_d, $fn = slot_fn);
    }
}

module top_column(p) {
    a = atan2(p[1], p[0]);
    zb = top_ledge_z0;
    translate([p[0], p[1], zb]) cylinder(d = boss_od, h = top_col_z1 - zb, $fn = 48);
    hull() {
        translate([p[0], p[1], zb]) cylinder(d = boss_od, h = 1, $fn = 48);
        rotate([0, 0, a]) translate([col_wedge_r, 0, col_corbel_z0]) cube(0.02, center = true);
    }
    rotate([0, 0, a]) {
        translate([top_screw_r, -col_bar_w / 2, zb]) cube([col_wedge_r - top_screw_r, col_bar_w, top_ledge_h]);
        translate([0, col_bar_w / 2, 0]) rotate([90, 0, 0]) linear_extrude(col_bar_w)
            polygon([
                [top_screw_r, zb + 0.01], [col_wedge_r, zb + 0.01],
                [col_wedge_r, zb - chamfer_slope * (col_wedge_r - top_screw_r)]
            ]);
    }
}

module top_column_cut(p) {
    translate([p[0], p[1], top_insert_bot_z]) cylinder(d = insert_d, h = top_insert_hole_depth + 1, $fn = 32);
}

module tray_boss(p) {
    translate([p[0], p[1], body_bot_z]) cylinder(d = boss_od, h = tray_boss_top_z - body_bot_z, $fn = 48);
}

module tray_boss_cut(p) {
    translate([p[0], p[1], body_bot_z - 1]) {
        cylinder(d = insert_d, h = tray_insert_hole_depth + 1, $fn = 32);
        translate([0, 0, tray_insert_hole_depth + 1 - 0.01])
            cylinder(d1 = insert_d, d2 = 0.02, h = tray_tip_h + 0.01, $fn = 32);
    }
}

module at_mic(az) {
    rotate([0, 0, az]) translate([0, 0, mic_z]) children();
}

module mic_fill(az) {
    at_mic(az) translate([mic_open_r, 0, 0]) rotate([0, 90, 0])
        cylinder(r = mic_fill_r, h = inner_r + 1 - mic_open_r, $fn = 96);
}

module mic_cut(az) {
    flare_d = mic_port_d + (mic_port_out_d - mic_port_d) * (mic_port_flare + 1) / mic_port_flare;
    at_mic(az) {
        translate([mic_open_r - 1, 0, 0]) rotate([0, 90, 0])
            cylinder(r = mic_pocket_r, h = mic_face_r - mic_open_r + 1, $fn = 96);
        translate([mic_face_r - 0.5, 0, 0]) rotate([0, 90, 0])
            cylinder(d = mic_port_d, h = case_r + 1 - mic_face_r, $fn = 32);
        translate([case_r - mic_port_flare, 0, 0]) rotate([0, 90, 0])
            cylinder(d1 = mic_port_d, d2 = flare_d, h = mic_port_flare + 1, $fn = 48);
    }
}

module body() {
    difference() {
        union() {
            tube();
            for (p = top_screw_xy) top_column(p);
            for (p = tray_screw_xy) tray_boss(p);
            for (az = [mic_az_l, mic_az_r]) mic_fill(az);
        }
        lit_cut();
        tail_slot(fil_tail_pts);
        tail_slot(fil_tail_pts_m);
        for (p = top_screw_xy) top_column_cut(p);
        for (p = tray_screw_xy) tray_boss_cut(p);
        for (az = [mic_az_l, mic_az_r]) mic_cut(az);
    }
}

module print_body() {
    translate([0, 0, -body_bot_z]) body();
}
