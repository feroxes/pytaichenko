include <params.scad>
use <lib.scad>

shell_fn = 180;

tip_h = insert_d / 2 * chamfer_slope;

tray_rib_w = 3;
tray_skin = 1.8;
tray_boss_h = tray_boss_hole_depth + tip_h + tray_skin;

pull_stop_gap = 0.4;
pull_stop_x0 = 9.8;
pull_stop_x1 = 13.8;
pull_stop_z0 = dk_z0 - 0.4;
pull_stop_z1 = dk_z1 + 0.4;
pull_stop_reach = 8;
pull_stop_env_z0 = dk_z0 - 5;
pull_stop_env_z1 = dk_z1 + 4.4;

function heel_arc(phi) = heel_c + heel_r * [sin(phi), -cos(phi)];

function body_face_r1() = plinth_r;

function tray_neck_r() = body_inner_r(0) - tray_gap;

function tray_flange_r() = body_face_r1() - tray_flange_lip;

function tray_gasket_in_r() = tray_neck_r() + tray_gasket_neck_clr;

function outer_pts() = concat(
    [[plinth_r, 0], edge_pt],
    [for (i = [1 : heel_n]) heel_arc(edge_phi + (edge_t_phi - edge_phi) * i / heel_n)],
    [for (z = [floor(edge_zt) + 1 : 1 : ring_z0]) [body_r(z), z]],
    ring_z0 > floor(ring_z0) ? [[body_r(ring_z0), ring_z0]] : []
);

function shell_poly() = let (o = outer_pts())
    concat(o, [for (i = [len(o) - 1 : -1 : 0]) [o[i][0] - wall_t, o[i][1]]]);

function envelope_poly() = concat([[0, 0]], outer_pts(), [[0, ring_z0]]);

module shell() {
    rotate_extrude($fn = shell_fn) polygon(shell_poly());
}

module sector(b, half) {
    rotate([0, 0, b - half]) rotate_extrude(angle = 2 * half, $fn = shell_fn) children();
}

module bridge_block(b) {
    rw = body_inner_r(ring_z0);
    zw = ring_z0 - 0.5;
    sector(b, bridge_half_deg) {
        polygon([
            [bridge_in_r, zw], [ring_outer_r, zw],
            [ring_outer_r, bridge_top_z], [bridge_in_r, bridge_top_z]
        ]);
        polygon([
            [bridge_in_r, zw + 0.01], [rw + 0.5, zw + 0.01],
            [rw + 0.5, zw - chamfer_slope * (rw + 0.5 - bridge_in_r)]
        ]);
    }
}

module bar_wedge(w) {
    zb = top_ledge_z0;
    u0 = top_screw_r;
    translate([0, w / 2, 0]) rotate([90, 0, 0]) linear_extrude(w)
        polygon([
            [u0, zb + 0.01], [wedge_out_r, zb + 0.01],
            [wedge_out_r, zb - chamfer_slope * (wedge_out_r - u0)]
        ]);
}

module top_column(b) {
    zb = top_ledge_z0;
    translate([polar(top_screw_r, b)[0], polar(top_screw_r, b)[1], zb])
        cylinder(d = boss_od, h = top_col_z1 - zb, $fn = 48);
    hull() {
        translate([polar(top_screw_r, b)[0], polar(top_screw_r, b)[1], zb])
            cylinder(d = boss_od, h = 1, $fn = 48);
        rotate([0, 0, b]) translate([wedge_out_r, 0, top_corbel_z0]) cube(0.02, center = true);
    }
    rotate([0, 0, b]) {
        translate([top_screw_r, -bar_w / 2, zb])
            cube([wedge_out_r - top_screw_r, bar_w, top_screw_ledge]);
        bar_wedge(bar_w);
    }
}

module top_column_cut(b) {
    translate([polar(top_screw_r, b)[0], polar(top_screw_r, b)[1], top_insert_bot_z])
        cylinder(d = insert_d, h = top_insert_hole_depth + 1, $fn = 32);
}

module chan_cuts() {
    chan_at_bridges() {
        chan_tube(chan_d, chan_len + chan_tail_l, chan_slot_l);
        chan_tube(chan_d, chan_open_l, chan_slot_l, 24, chan_open_up);
    }
}

module chan_checks() {
    assert(min([for (u = [0 : 0.25 : chan_len + chan_tail_l]) let (q = chan_pt(u))
        body_r(min(q[2], ring_z0)) - norm([q[0], q[1]])]) - chan_d / 2 >= 0.5);
    assert(norm([chan_p1[0], chan_p1[1]]) - body_inner_r(chan_p1[2]) <= chan_d / 2 + 0.1);
    assert(chan_p2[2] < ring_z0 - fil_contact);
    assert(top_screw_r + boss_od / 2 < body_inner_r(top_ledge_z0) - 0.5);
    assert(wedge_out_r > body_inner_r(top_corbel_z0) + 0.5 && wedge_out_r < body_r(top_corbel_z0) - 0.5);
    assert(min([for (b = bridge_angles, m = [-1, 1], a = top_screw_angles, u = [0 : 0.5 : chan_len + chan_tail_l])
        let (q = chan_pt(u), w = [q[0] * cos(b) - m * q[1] * sin(b), q[0] * sin(b) + m * q[1] * cos(b)])
            norm(w - top_screw_r * [cos(a), sin(a)])])
        - boss_od / 2 - chan_d / 2 >= 0.5);
}

function rib_env_pts() = let(z0 = tray_pad_t - 1, zt = tray_pad_t + tray_boss_h + 1) concat(
    [[0, z0]],
    [for (z = [z0 : 1 : zt]) [body_inner_r(z) + 1, z]],
    [[0, zt]]
);

module rib_envelope() {
    rotate_extrude($fn = shell_fn) polygon(rib_env_pts());
}

module tray_boss(a) {
    rotate([0, 0, a]) {
        translate([tray_screw_r, 0, tray_pad_t]) cylinder(d = boss_od, h = tray_boss_h, $fn = 48);
        intersection() {
            translate([tray_screw_r, -tray_rib_w / 2, tray_pad_t])
                cube([ring_outer_r - tray_screw_r, tray_rib_w, tray_boss_h]);
            rib_envelope();
        }
    }
}

module tray_boss_cut(a) {
    rotate([0, 0, a]) translate([tray_screw_r, 0, tray_pad_t - 1]) {
        cylinder(d = insert_d, h = tray_boss_hole_depth + 1, $fn = 32);
        translate([0, 0, tray_boss_hole_depth + 1 - 0.01])
            cylinder(d1 = insert_d, d2 = 0.02, h = tip_h + 0.01, $fn = 32);
    }
}

function pull_stop_env_pts() = concat(
    [[0, pull_stop_env_z0]],
    [for (z = [pull_stop_env_z0 : 1 : pull_stop_env_z1]) [body_inner_r(z) + wall_t / 2, z]],
    [[0, pull_stop_env_z1]]
);

module usb_pull_stops() {
    y_face = dk_y1() + pull_stop_gap;
    for (sx = [-1, 1]) scale([sx, 1, 1]) intersection() {
        translate([pull_stop_x0, 0, 0]) rotate([90, 0, 90])
            linear_extrude(pull_stop_x1 - pull_stop_x0)
                polygon([
                    [y_face, pull_stop_z0], [y_face, pull_stop_z1],
                    [y_face + pull_stop_reach, pull_stop_z1],
                    [y_face + pull_stop_reach,
                        pull_stop_z0 - chamfer_slope * pull_stop_reach]
                ]);
        rotate_extrude($fn = shell_fn) polygon(pull_stop_env_pts());
    }
}

module usb_window() {
    y0 = usb_tip_y() - 0.5;
    y1 = body_r(usb_win_z1) + 2;
    translate([-usb_win_w / 2, y0, usb_win_z0]) cube([usb_win_w, y1 - y0, usb_win_h]);
}

module mic_cuts() {
    rotate([0, 0, mic_az]) translate([mic_face_r() - 1, 0, mic_z]) rotate([0, 90, 0])
        cylinder(d = mic_port_d, h = body_r(mic_z) + 3 - mic_face_r() + 1, $fn = 32);
    intersection() {
        rotate([0, 0, mic_az]) translate([mic_face_r() - 3, 0, mic_z]) rotate([0, 90, 0])
            cylinder(d = mic_seat_d, h = 12, $fn = 64);
        rotate_extrude($fn = shell_fn)
            polygon(concat(
                [[0, mic_z - 9]],
                [for (z = [mic_z - 9 : 1 : mic_z + 9]) [body_inner_r(z) + mic_seat_depth, z]],
                [[0, mic_z + 9]]
            ));
    }
}

module base_checks() {
    assert(body_face_r1() - body_inner_r(0) >= lip_face_need - 1e-9);
    assert(abs(body_r(edge_pt[1]) - edge_pt[0]) < 1e-6);
    assert(abs(body_r(edge_zt - 1e-9) - edge_out_r) < 1e-4);
    assert(abs(bowl_r(edge_zt) - edge_out_r) < 1e-6);
    assert(abs(body_dr(edge_zt - 1e-6) - bowl_dr(edge_zt)) < 1e-3);
    assert(abs(body_dr(plinth_h + 1e-6) - 1 / tan(edge_phi)) < 1e-3);
}

module body() {
    chan_checks();
    base_checks();
    difference() {
        union() {
            shell();
            for (b = bridge_angles) bridge_block(b);
            for (b = top_screw_angles) top_column(b);
            for (a = tray_screw_angles) tray_boss(a);
            usb_pull_stops();
        }
        for (b = top_screw_angles) top_column_cut(b);
        chan_cuts();
        for (a = tray_screw_angles) tray_boss_cut(a);
        usb_window();
        mic_cuts();
    }
}

module print_body() {
    body();
}
