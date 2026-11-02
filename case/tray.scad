include <params.scad>
use <lib.scad>
use <dummies.scad>
use <body.scad>

tray_fn = 180;
tr_eps = 0.01;
tr_fillet_lift = 0.02;
tr_relief_w = 0.2;
tr_relief_h = 0.6;

tr_wall = 1.2;

tr_dk_pad_x0 = 10.2;
tr_dk_pad_depth = 1.4;
tr_dk_wall_len = 8;
tr_dk_out = dk_w / 2 + clr + tr_wall;
tr_dk_back_x = 7.4;
tr_dk_back_pad_depth = 1.2;
tr_dk_back_wall = 1.0;
tr_dk_back_trim = 0.5;

tr_r0 = tray_neck_r();

function tray_floor_pts() = [
    [0, tray_bot_z],
    [tray_flange_r(), tray_bot_z],
    [tray_flange_r(), -tray_gasket_t],
    [tr_r0, -tray_gasket_t],
    [tr_r0, tray_floor_t - tr_relief_h],
    [tr_r0 - tr_relief_w, tray_floor_t],
    [0, tray_floor_t]
];

module tray_floor() {
    rotate_extrude($fn = tray_fn) polygon(tray_floor_pts());
}

module screw_cut(a) {
    at_polar(tray_screw_r, a) {
        translate([0, 0, tray_bot_z - 1]) cylinder(d = csk_d, h = tray_screw_recess + 1, $fn = 48);
        translate([0, 0, tray_seat_z]) screw_seat();
        translate([0, 0, tray_bot_z - 1])
            cylinder(d = screw_thru_d, h = tray_pad_top_z - tray_bot_z + 2, $fn = 32);
        translate([0, 0, tray_pad_top_z - tray_pad_relief_depth])
            cylinder(d = tray_pad_relief_d, h = tray_pad_relief_depth + 1, $fn = 48);
    }
}

module niche_cut(a) {
    rotate([0, 0, a]) translate([0, 0, tray_bot_z - 1]) linear_extrude(foot_niche_h + 1)
        hull() {
            translate([foot_r, 0]) foot2d(foot_niche_clr);
            translate([foot_r + foot_l, 0]) foot2d(foot_niche_clr);
        }
}

function latch_fillet_pts(rf, xc, zc) = [
    for (i = [0 : 8]) let (a = 180 + 90 * i / 8) [xc + rf * cos(a), zc + rf * sin(a)]
];

module latch(w, board_top, ov, t, c = clr, relief = latch_relief) {
    zr = tray_floor_t - relief;
    zu = board_top + latch_vg - latch_k * c;
    zt = zu + latch_k * (ov + c);
    zh = zt + (ov + c);
    rf = latch_root_fillet;
    rotate([90, 0, 0]) linear_extrude(w, center = true)
        polygon(concat(
            [[-c - t, zr - tr_eps], [-c - t, zh], [-c, zh], [ov, zt], [-c, zu]],
            latch_fillet_pts(rf, -c + rf, zr + rf + tr_fillet_lift),
            [[-c + rf, zr - tr_eps]]
        ));
}

module latch_relief_cut(w, t, c = clr, relief = latch_relief) {
    zr = tray_floor_t - relief;
    x0 = -c - t - latch_relief_clr;
    x1 = -c + latch_root_fillet + latch_relief_clr;
    rotate([90, 0, 0]) linear_extrude(w + 2 * latch_relief_clr, center = true)
        translate([x0, zr]) square([x1 - x0, relief + 1]);
}

module dk_front_frame() {
    translate([0, dk_y0(), 0]) rotate([0, 0, 90]) children();
}

module dk_back_frames() {
    for (sx = [-1, 1]) translate([sx * tr_dk_back_x, dk_y1(), 0]) rotate([0, 0, -90]) children();
}

module amp_frames() {
    for (sy = [-1, 1], sx = [-1, 1])
        translate([amp_x + sx * amp_latch_dx, amp_y + sy * amp_l / 2, 0])
            rotate([0, 0, sy > 0 ? -90 : 90]) children();
}

module latch_reliefs() {
    dk_front_frame() latch_relief_cut(dkf_latch_w, dkf_latch_t);
    dk_back_frames() latch_relief_cut(dkb_latch_w, dkb_latch_t);
    amp_frames() latch_relief_cut(amp_latch_w, amp_latch_t, amp_clr, amp_latch_relief);
}

module dk_front_corner() {
    y0 = dk_y0();
    in_x = dk_w / 2 + clr;
    box(tr_dk_pad_x0, tr_dk_out, y0 - clr - tr_wall, y0 + tr_dk_pad_depth,
        tray_floor_t - tr_eps, dk_z0);
    box(tr_dk_pad_x0, tr_dk_out, y0 - clr - tr_wall, y0 - clr, tray_floor_t - tr_eps, dk_z1);
    box(in_x, tr_dk_out, y0 - clr - tr_wall, y0 + tr_dk_wall_len, tray_floor_t - tr_eps, dk_z1);
}

module dk_back_corner() {
    y1 = dk_y1();
    in_x = dk_w / 2 + clr;
    box(tr_dk_pad_x0, in_x, y1 - clr - tr_dk_back_pad_depth, y1 - clr,
        tray_floor_t - tr_eps, dk_z0);
    box(in_x, in_x + tr_dk_back_wall, y1 - tr_dk_back_trim - tr_dk_wall_len,
        y1 - tr_dk_back_trim, tray_floor_t - tr_eps, dk_z1);
}

module dk_standoff() {
    y = dk_y0() + dk_hole_in;
    intersection() {
        translate([dk_hole_x, y, tray_floor_t - tr_eps])
            cylinder(d = boss_od, h = dk_z0 - tray_floor_t + tr_eps, $fn = 48);
        box(dk_module_w / 2 + dk_mod_clr, dk_hole_x + boss_od, y - boss_od, y + boss_od,
            tray_floor_t - 2 * tr_eps, dk_z0);
    }
}

module dk_insert_cut() {
    translate([dk_hole_x, dk_y0() + dk_hole_in, dk_z0 - dk_insert_hole_depth])
        cylinder(d = insert_d, h = dk_insert_hole_depth + 1, $fn = 32);
}

module devkit_cradle() {
    for (sx = [-1, 1]) scale([sx, 1, 1]) {
        dk_front_corner();
        dk_back_corner();
        dk_standoff();
    }
    dk_front_frame() latch(dkf_latch_w, dk_z1, dkf_latch_ov, dkf_latch_t);
    dk_back_frames() latch(dkb_latch_w, dk_z1, dkb_latch_ov, dkb_latch_t);
}

module amp_corner() {
    hx = amp_w / 2;
    hy = amp_l / 2;
    zt = amp_z0 + amp_t;
    z0 = tray_floor_t - tr_eps;
    box(-hx + amp_hdr_in + amp_hdr_w + amp_hdr_pad_gap,
        -hx + amp_hdr_in + amp_hdr_w + amp_hdr_pad_gap + amp_hdr_pad_w, hy - amp_pad, hy, z0, amp_z0);
    box(hx - amp_pad, hx, hy - amp_pad, hy, z0, amp_z0);
    box(-hx, -hx + amp_seg_len, hy + amp_clr, hy + amp_clr + tr_wall, z0, zt);
    box(hx - amp_seg_len, hx, hy + amp_clr, hy + amp_clr + tr_wall, z0, zt);
    box(hx + amp_clr, hx + amp_clr + tr_wall, hy - amp_stop_len, hy, z0, zt);
    box(-hx - amp_clr - tr_wall, -hx - amp_clr, hy - amp_stop_len, hy, z0, zt);
}

module amp_cradle() {
    for (sy = [-1, 1]) translate([amp_x, amp_y, 0]) scale([1, sy, 1]) amp_corner();
    amp_frames() latch(amp_latch_w, amp_z0 + amp_t, amp_latch_ov, amp_latch_t, amp_clr, amp_latch_relief);
}

module tray() {
    difference() {
        union() {
            difference() {
                tray_floor();
                latch_reliefs();
            }
            devkit_cradle();
            amp_cradle();
        }
        for (a = tray_screw_angles) screw_cut(a);
        for (a = foot_angles) niche_cut(a);
        for (sx = [-1, 1]) scale([sx, 1, 1]) dk_insert_cut();
    }
}

module print_tray() {
    translate([0, 0, -tray_bot_z]) tray();
}
