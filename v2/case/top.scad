include <params.scad>
use <lib.scad>

top_fn = 360;
hole_fn = 32;
tp_eps = 0.01;
root_over = 0.02;
fill_lift = 0.05;
edge_steps = 16;

function arc_pt(psi) = [
    case_r - top_edge_r + top_edge_r * cos(psi),
    top_edge_z0 + top_edge_r * sin(psi)
];

function top_profile() = concat(
    [[0, grille_bot_z], [spk_recess_r, grille_bot_z], [spk_recess_r, top_under_z],
     [top_skirt_in_r, top_under_z], [top_skirt_in_r, top_z0], [case_r, top_z0]],
    [for (i = [0 : edge_steps]) arc_pt(top_edge_psi * i / edge_steps)],
    [[top_face_r, H], [0, H]]
);

function root_arc(cy, cz, r, t0, t1) = [
    for (i = [0 : 10]) [cy + r * cos(t0 + (t1 - t0) * i / 10), cz + r * sin(t0 + (t1 - t0) * i / 10)]
];

function tab_pts() = concat(
    [[btn_in - btn_hook_u, btn_z0], [btn_in, btn_z0]],
    root_arc(btn_in - btn_root_in, btn_root_z - btn_root_in + root_over, btn_root_in, 0, 90),
    [[btn_in - btn_root_in, btn_root_z + 3 * root_over],
     [btn_in + btn_tab_t + btn_root_out, btn_root_z + 3 * root_over]],
    root_arc(btn_in + btn_tab_t + btn_root_out, btn_root_z - btn_root_out + root_over, btn_root_out, 90, 180),
    [[btn_in + btn_tab_t, btn_hook_z0], [btn_in, btn_hook_z0]]
);

module tp_at_button() {
    rotate([0, 0, button_az]) translate([button_r, 0, 0]) children();
}

module lid_shell() {
    rotate_extrude($fn = top_fn) polygon(top_profile());
}

module gasket_lip() {
    translate([0, 0, spk_lip_z0]) difference() {
        cylinder(r = spk_lip_in_r + spk_lip_w, h = top_under_z + tp_eps - spk_lip_z0, $fn = top_fn);
        translate([0, 0, -1]) cylinder(r = spk_lip_in_r, h = top_under_z - spk_lip_z0 + 2, $fn = top_fn);
    }
}

module ear_standoffs() {
    for (p = spk_ear_xy) translate([p[0], p[1], spk_standoff_z0])
        cylinder(d = boss_od, h = top_under_z + tp_eps - spk_standoff_z0, $fn = 48);
}

module ear_insert_holes() {
    for (p = spk_ear_xy) translate([p[0], p[1], spk_standoff_z0 - 1])
        cylinder(d = insert_d, h = spk_insert_hole_depth + 1, $fn = 32);
}

module top_screw_cuts() {
    for (p = top_screw_xy) translate([p[0], p[1], 0]) {
        translate([0, 0, top_under_z - 1]) cylinder(d = top_sock_d, h = top_boss_z0 - top_under_z + 1, $fn = 64);
        translate([0, 0, top_boss_z0 - 1]) cylinder(d = screw_thru_d, h = H - top_boss_z0 + 2, $fn = 32);
        translate([0, 0, top_seat_z]) mirror([0, 0, 1]) screw_seat();
        translate([0, 0, top_seat_z]) cylinder(d = top_plug_d, h = top_plug_depth + 1, $fn = 64);
    }
}

module top_gasket_groove() {
    rotate_extrude($fn = top_fn) translate([top_groove_in_r, top_z0 - 1])
        square([top_groove_out_r - top_groove_in_r, top_groove_depth + 1]);
}

module button_pocket() {
    tp_at_button() box(-btn_in, btn_in, -btn_in, btn_in, top_z0 - 1, btn_ceil_z);
}

module button_holder() {
    slit_x = btn_tab_w / 2 + btn_tab_slit;
    tp_at_button() {
        box(-btn_out, btn_skirt_x, -btn_out, btn_out, btn_ceil_z, top_under_z + tp_eps);
        box(-btn_out, -btn_in, -btn_out, btn_out, btn_z0, btn_ceil_z + tp_eps);
        box(btn_in - 0.1, btn_skirt_x, -btn_out, btn_out, top_z0 + fill_lift, btn_ceil_z + tp_eps);
        for (s = [-1, 1], x0 = [-btn_in, slit_x])
            box(x0, x0 + btn_in - slit_x, s > 0 ? btn_in : -btn_out, s > 0 ? btn_out : -btn_in,
                btn_z0, btn_ceil_z + tp_eps);
    }
}

module button_cuts() {
    tp_at_button() {
        translate([0, 0, btn_ceil_z - 1]) cylinder(d = btn_bore_d, h = btn_plate_z - btn_ceil_z + 1, $fn = 64);
        translate([0, 0, btn_plate_z - 0.5]) cylinder(d = button_hole_d, h = top_plate_t + 1, $fn = 64);
        for (s = [-1, 1]) {
            box(-btn_relief_x, btn_relief_x,
                s > 0 ? btn_in - btn_tab_cut_in : -btn_out - tp_eps,
                s > 0 ? btn_out + tp_eps : -(btn_in - btn_tab_cut_in),
                btn_ceil_z - 1, btn_plate_z);
            box(-btn_relief_x, btn_relief_x,
                s > 0 ? btn_relief_y0 : -btn_relief_y1,
                s > 0 ? btn_relief_y1 : -btn_relief_y0,
                btn_relief_z0, btn_root_z);
        }
    }
}

module snap_tabs(flex) {
    flex_deg = asin(btn_tab_flex / btn_tab_len);
    for (m = [0, 1]) mirror([0, m, 0])
        translate([0, btn_in, btn_root_z])
            rotate([flex ? flex_deg : 0, 0, 0])
                translate([0, -btn_in, -btn_root_z])
                    rotate([90, 0, 90]) linear_extrude(btn_tab_w, center = true) polygon(tab_pts());
}

module dimple(d) {
    lift = 1;
    translate([0, 0, H - sf_dimple_depth])
        cylinder(d1 = d - sf_dimple_floor_shrink, d2 = d + 2 * lift / tan(sf_dimple_wall),
                 h = sf_dimple_depth + lift, $fn = hole_fn);
}

module sunflower_cuts() {
    for (p = sf_pts) translate([p[0], p[1], 0]) {
        if (p[3] == 1) translate([0, 0, top_under_z - 0.5]) cylinder(d = p[2], h = H - top_under_z + 1, $fn = hole_fn);
        else dimple(p[2]);
    }
}

module top(flex = false) {
    union() {
        difference() {
            union() {
                lid_shell();
                gasket_lip();
                ear_standoffs();
                button_holder();
            }
            button_pocket();
            ear_insert_holes();
            top_screw_cuts();
            button_cuts();
            sunflower_cuts();
            top_gasket_groove();
        }
        tp_at_button() snap_tabs(flex);
    }
}

module print_top() {
    translate([0, 0, H]) rotate([180, 0, 0]) top();
}
