include <params.scad>
use <lib.scad>

tray_fn = 360;
tr_eps = 0.01;

module tray_shell() {
    rotate_extrude($fn = tray_fn) polygon([
        [0, 0], [case_r, 0], [case_r, body_bot_z], [inner_r, body_bot_z], [inner_r, tray_floor_t], [0, tray_floor_t]
    ]);
}

module inside(r = inner_r + 0.5) {
    intersection() {
        children();
        translate([0, 0, -1]) cylinder(r = r, h = body_bot_z + 2, $fn = tray_fn);
    }
}

module rails() {
    inside() for (s = [-1, 1])
        box(s > 0 ? tray_rail_in_x : -tray_rail_out_x, s > 0 ? tray_rail_out_x : -tray_rail_in_x,
            -case_r, tray_rail_y1, tray_floor_t - tr_eps, tray_rail_top_z);
}

module rail_cuts() {
    for (s = [-1, 1]) {
        box(s * dk_row_x - tray_slot_w / 2, s * dk_row_x + tray_slot_w / 2, tray_slot_y0, tray_slot_y1,
            dk_hdr_z0, dk_z0);
        box(s * dk_row_x - tray_pin_groove_w / 2, s * dk_row_x + tray_pin_groove_w / 2,
            tray_pin_groove_y0, tray_pin_groove_y1, tray_pin_floor_z, dk_hdr_z0 + tr_eps);
    }
}

module latch(w, ov, t, c, top = dk_z1) {
    zr = tray_latch_root_z;
    zu = top + latch_vg - latch_k * c;
    zt = zu + latch_k * (ov + c);
    zh = zt + (ov + c);
    r = latch_root_fillet;
    rotate([90, 0, 0]) linear_extrude(w, center = true)
        polygon(concat(
            [for (i = [0 : 10]) let (a = -90 + 9 * i) [-c - t - r + r * cos(a), zr + r + r * sin(a)]],
            [[-c - t, zh], [-c, zh], [ov, zt], [-c, zu], [-c, zr]]));
}

module side_frames() {
    for (s = [-1, 1]) translate([s * dk_w / 2, dks_latch_y, 0]) rotate([0, 0, s > 0 ? 180 : 0]) children();
}

module front_frames() {
    for (s = [-1, 1]) translate([s * (dkf_latch_x0 + dkf_latch_x1) / 2, dk_y1, 0]) rotate([0, 0, -90]) children();
}

module latch_windows() {
    side_frames() box(-(tray_rail_out_x - dk_w / 2) - 0.3, 0,
        -dks_latch_w / 2 - latch_relief_clr, dks_latch_w / 2 + latch_relief_clr,
        tray_latch_root_z, tray_rail_top_z + 1);
    front_frames() box(-(dkf_latch_c + dkf_latch_t + latch_root_fillet + latch_relief_clr), dkf_rail_cut,
        -(dkf_latch_x1 - dkf_latch_x0) / 2 - latch_side_clr, (dkf_latch_x1 - dkf_latch_x0) / 2 + latch_side_clr,
        tray_latch_root_z, tray_rail_top_z + 1);
    amp_frame(amp_latch_lx, 1) box(-(amp_clr + tray_amp_stop_t) - 0.3, 0,
        -amp_latch_w / 2 - latch_side_clr, amp_latch_w / 2 + latch_side_clr,
        tray_latch_root_z, tray_amp_stop_top_z + 1);
}

module amp_frame(lx, side) {
    translate([amp_xy[0], amp_xy[1], 0]) rotate([0, 0, amp_rot]) translate([lx, side * amp_l / 2, 0])
        rotate([0, 0, -90 * side]) children();
}

module latches() {
    side_frames() latch(dks_latch_w, dks_latch_ov, dks_latch_t, dks_latch_c);
    front_frames() latch(dkf_latch_x1 - dkf_latch_x0, dkf_latch_ov, dkf_latch_t, dkf_latch_c);
    amp_frame(amp_latch_lx, 1) latch(amp_latch_w, amp_latch_ov, amp_latch_t, amp_latch_c, amp_z0 + amp_t);
}

module amp_lip() {
    hw = amp_lip_w / 2;
    amp_frame(amp_lip_lx, -1) {
        box(-amp_clr - tr_eps, amp_lip_ov, -hw, hw, amp_lip_z, tray_rail_top_z);
        box(-amp_clr - tr_eps, -amp_lip_gap, -hw, hw, tray_floor_t - tr_eps, amp_lip_z + tr_eps);
    }
}

module amp_nest() {
    hw = amp_w / 2;
    hl = amp_l / 2;
    c = amp_clr;
    t = tray_amp_stop_t;
    z0 = tray_floor_t - tr_eps;
    inside(case_r - 0.5) translate([amp_xy[0], amp_xy[1], 0]) rotate([0, 0, amp_rot]) {
        box(-hw, tray_amp_pad_lx1, -hl, hl, z0, amp_z0);
        box(tray_amp_bar_lx0, hw, -hl, hl, z0, amp_z0);
        box(hw + c, hw + c + t, -hl - c, hl + c + t, z0, tray_amp_stop_top_z);
        box(-hw - c - t, hw + c + t, hl + c, hl + c + t, z0, tray_amp_stop_top_z);
        box(-hw - c - t, -hw - c, -hl - c, hl + c + t, z0, tray_amp_stop_top_z);
    }
}

module columns() {
    inside(inner_r + 0.5) for (p = tray_screw_xy) translate([p[0], p[1], 0]) cylinder(d = tray_col_od, h = body_bot_z - tray_col_drop, $fn = 64);
}

module screw_cut(p) {
    translate([p[0], p[1], 0]) {
        translate([0, 0, -1]) cylinder(d = tray_access_d, h = tray_access_z1 + 1, $fn = 64);
        translate([0, 0, tray_access_z1 - tr_eps])
            cylinder(d1 = tray_access_d, d2 = csk_d, h = tray_seat_z - tray_access_z1 + tr_eps, $fn = 64);
        translate([0, 0, tray_seat_z]) screw_seat();
        translate([0, 0, tray_seat_z]) cylinder(d = screw_thru_d, h = body_bot_z - tray_seat_z + 1, $fn = 32);
    }
}

module com_cut() {
    box(dk_com_x - tray_com_w / 2, dk_com_x + tray_com_w / 2, -case_r - 1, usb_face_y,
        usb_z - tray_com_h / 2, usb_z + tray_com_h / 2);
}

module groove_cut() {
    translate([0, 0, body_bot_z - tray_groove_depth]) rotate_extrude($fn = tray_fn)
        translate([tray_groove_in_r, 0]) square([tray_groove_out_r - tray_groove_in_r, tray_groove_depth + 1]);
}

module at_feet() {
    for (i = [0 : len(foot_xy) - 1])
        translate([foot_xy[i][0], foot_xy[i][1], 0]) rotate([0, 0, foot_rot[i]]) children();
}

module foot_pads() {
    at_feet() linear_extrude(foot_pad_h) foot2d(foot_niche_clr + foot_pad_grow);
}

module niche_cuts() {
    at_feet() translate([0, 0, -1]) linear_extrude(foot_niche_h + 1) foot2d(foot_niche_clr);
}

module tray() {
    difference() {
        union() {
            difference() {
                union() {
                    tray_shell();
                    rails();
                    amp_nest();
                    amp_lip();
                    columns();
                    foot_pads();
                }
                rail_cuts();
                latch_windows();
            }
            latches();
        }
        for (p = tray_screw_xy) screw_cut(p);
        com_cut();
        groove_cut();
        niche_cuts();
    }
}

module print_tray() {
    tray();
}
