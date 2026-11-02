include <params.scad>
use <lib.scad>

top_fn = 360;
rim_fn = 180;
hole_fn = 24;
tp_eps = 0.01;
edge_steps = 16;

grid_rows = ceil(dimple_r / (grille_pitch * sqrt(3) / 2)) + 1;
grid_cols = ceil(dimple_r / grille_pitch) + 1;
dimple_k = (dimple_d / 2) / dimple_depth;

function arc_pt(psi) = [
    ring_outer_r - top_edge_r + top_edge_r * cos(psi),
    top_edge_z0 + top_edge_r * sin(psi)
];

function top_profile() = concat(
    [[0, top_underside_z], [top_skirt_in_r, top_underside_z],
     [top_skirt_in_r, top_z0], [ring_outer_r, top_z0]],
    [for (i = [0 : edge_steps]) arc_pt(top_edge_psi * i / edge_steps)],
    [[top_face_r, H], [0, H]]
);

function hex_pts() = [
    for (j = [-grid_rows : grid_rows], i = [-grid_cols : grid_cols])
        let (x = i * grille_pitch + (abs(j) % 2) * grille_pitch / 2,
             y = j * grille_pitch * sqrt(3) / 2)
            if (norm([x, y]) <= dimple_r) [x, y]
];

function keep_centres() = [for (a = spk_hole_angles) polar(spk_hole_r, a)];

function plug_centres() = [for (a = top_screw_angles) polar(top_screw_r, a)];

function near_any(pt, centres, d) = len([for (c = centres) if (norm(pt - c) < d) 1]) > 0;

function btn_local(pt) = let (q = pt - polar(button_r, button_az))
    [q[0] * cos(button_az) + q[1] * sin(button_az), -q[0] * sin(button_az) + q[1] * cos(button_az)];

function relief_dist(pt) = let (q = btn_local(pt), y = abs(q[1]))
    norm([max(abs(q[0]) - btn_relief_x, 0), max(btn_relief_y0 - y, 0, y - btn_relief_y1)]);

function dimple_ok(pt) =
    !near_any(pt, keep_centres(), dimple_keep)
    && relief_dist(pt) >= dimple_d / 2 + btn_rim_min
    && !near_any(pt, plug_centres(), top_plug_keep)
    && norm(pt - polar(button_r, button_az)) >= button_hole_d / 2 + dimple_d / 2 + 0.8;

module tp_at(r, a) {
    let (p = polar(r, a)) translate([p[0], p[1], 0]) children();
}

module tp_at_button() {
    rotate([0, 0, button_az]) translate([button_r, 0, 0]) children();
}

module top_shell() {
    rotate_extrude($fn = top_fn) polygon(top_profile());
}

module speaker_seat() {
    translate([0, 0, gasket_z1]) linear_extrude(top_underside_z + tp_eps - gasket_z1)
        difference() {
            spk_outline2d(-spk_seat_flange_clr);
            circle(r = gasket_inner_r, $fn = rim_fn);
        }
}

module speaker_boss(a) {
    tp_at(spk_hole_r, a) translate([0, 0, gasket_z1])
        cylinder(d = boss_od, h = top_underside_z + tp_eps - gasket_z1, $fn = 48);
}

module speaker_insert_hole(a) {
    tp_at(spk_hole_r, a) translate([0, 0, gasket_z1 - 1])
        cylinder(d = insert_d, h = spk_insert_hole_depth + 1, $fn = 32);
}

module screw_boss(a) {
    tp_at(top_screw_r, a) translate([0, 0, top_boss_z0])
        cylinder(d = boss_od, h = top_underside_z + tp_eps - top_boss_z0, $fn = 48);
}

module screw_hole(a) {
    tp_at(top_screw_r, a) {
        translate([0, 0, top_boss_z0 - 1]) cylinder(d = screw_thru_d, h = H - top_boss_z0 + 2, $fn = 32);
        translate([0, 0, top_seat_z]) mirror([0, 0, 1]) screw_seat();
        translate([0, 0, top_seat_z]) cylinder(d = top_plug_d, h = top_plug_depth + 1, $fn = 64);
    }
}

function root_arc(cy, cz, r, t0, t1) = [
    for (i = [0 : 10]) [cy + r * cos(t0 + (t1 - t0) * i / 10), cz + r * sin(t0 + (t1 - t0) * i / 10)]
];

function tab_pts() = concat(
    [[btn_in - btn_hook_u, btn_z0], [btn_in, btn_z0]],
    root_arc(btn_in - btn_root_in, btn_root_z - btn_root_in, btn_root_in, 0, 90),
    [[btn_in - btn_root_in, btn_root_z + tp_eps],
     [btn_out + btn_root_out, btn_root_z + tp_eps]],
    root_arc(btn_out + btn_root_out, btn_root_z - btn_root_out, btn_root_out, 90, 180),
    [[btn_out, btn_hook_z0], [btn_in, btn_hook_z0]]
);

module tab_profile() {
    polygon(tab_pts());
}

module snap_tabs(flex) {
    flex_deg = asin(btn_tab_flex / btn_tab_len);
    for (m = [0, 1]) mirror([0, m, 0])
        translate([0, btn_in, btn_root_z])
            rotate([flex ? flex_deg : 0, 0, 0])
                translate([0, -btn_in, -btn_root_z])
                    rotate([90, 0, 90]) linear_extrude(btn_tab_w, center = true) tab_profile();
}

module button_holder() {
    h_top = top_underside_z + tp_eps;
    slit_x = btn_tab_w / 2 + btn_tab_slit;
    tp_at_button() {
        difference() {
            union() {
                translate([-btn_out, -btn_out, btn_ceil_z])
                    cube([btn_out + btn_skirt_x, 2 * btn_out, h_top - btn_ceil_z]);
                translate([-btn_out, -btn_out, btn_z0])
                    cube([btn_out - btn_in, 2 * btn_out, btn_ceil_z + tp_eps - btn_z0]);
                if (btn_skirt_x > btn_in)
                    translate([btn_in, -btn_out, top_z0])
                        cube([btn_skirt_x - btn_in, 2 * btn_out, btn_ceil_z + tp_eps - top_z0]);
                for (s = [-1, 1], x0 = [-btn_in, slit_x])
                    translate([x0, s > 0 ? btn_in : -btn_out, btn_z0])
                        cube([btn_in - slit_x, btn_wall, btn_ceil_z + tp_eps - btn_z0]);
            }
            for (s = [-1, 1]) {
                y0 = s > 0 ? btn_in - 2.2 : -btn_out - tp_eps;
                ylen = btn_wall + 2.2 + tp_eps;
                translate([-btn_tab_w / 2, y0, btn_ceil_z - 1])
                    cube([btn_tab_w, ylen, top_underside_z - btn_ceil_z + 1]);
                for (sx = [-1, 1]) {
                    cx = sx * (btn_tab_w / 2 + btn_tab_slit / 2);
                    r = btn_tab_slit / 2;
                    translate([cx - r, y0, btn_ceil_z - 1])
                        cube([2 * r, ylen, top_underside_z - btn_ceil_z + 1]);
                }
            }
        }
    }
}

module tab_reliefs() {
    tp_at_button() for (m = [0, 1]) mirror([0, m, 0])
        translate([-btn_relief_x, btn_relief_y0, top_underside_z - 0.5])
            cube([2 * btn_relief_x, btn_relief_y1 - btn_relief_y0, btn_root_z - top_underside_z + 0.5]);
}

module button_pocket() {
    tp_at_button() translate([-btn_in, -btn_in, top_z0 - 1])
        cube([2 * btn_in, 2 * btn_in, btn_ceil_z - top_z0 + 1]);
}

module button_collar() {
    tp_at_button() translate([0, 0, H - btn_collar_h])
        cylinder(r = btn_collar_r, h = btn_collar_h, $fn = 96);
}

module button_cuts() {
    tp_at_button() {
        translate([0, 0, btn_ceil_z - 1])
            cylinder(d = btn_bore_d, h = top_underside_z - btn_ceil_z + 1, $fn = 64);
        translate([0, 0, top_underside_z - 0.5])
            cylinder(d = button_hole_d, h = top_plate_t + 1, $fn = 64);
    }
}

module grille_cuts() {
    for (pt = hex_pts()) {
        if (norm(pt) <= grille_r)
            translate([pt[0], pt[1], top_underside_z - 0.5])
                cylinder(d = grille_hole_d, h = top_plate_t + 1, $fn = hole_fn);
        else if (dimple_ok(pt))
            translate([pt[0], pt[1], H - dimple_depth])
                cylinder(r1 = 0.01, r2 = 0.01 + dimple_k * (dimple_depth + tp_eps),
                    h = dimple_depth + tp_eps, $fn = hole_fn);
    }
}

module top(grille = true, flex = false) {
    union() {
        difference() {
            union() {
                difference() {
                    top_shell();
                    button_pocket();
                }
                speaker_seat();
                for (a = spk_hole_angles) speaker_boss(a);
                for (a = top_screw_angles) screw_boss(a);
                button_holder();
                button_collar();
            }
            for (a = spk_hole_angles) speaker_insert_hole(a);
            for (a = top_screw_angles) screw_hole(a);
            button_cuts();
            tab_reliefs();
            if (grille) grille_cuts();
        }
        tp_at_button() snap_tabs(flex);
    }
}

module print_top() {
    translate([0, 0, H]) rotate([180, 0, 0]) top();
}

module echo_top() {
    echo(arc_top_in_r=arc_top_in_r, top_skirt_in_r=top_skirt_in_r, top_edge_r=top_edge_r);
    echo(top_edge_psi=top_edge_psi, top_bevel_l=top_bevel_l, top_edge_z0=top_edge_z0);
    echo(top_face_r=top_face_r, top_plug_keep=top_plug_keep);
    echo(btn_rim_min=btn_rim_min, btn_collar_facet=btn_collar_facet, btn_collar_r=btn_collar_r, btn_collar_h=btn_collar_h);
    echo(dimple_d=dimple_d, dimple_depth=dimple_depth, dimple_r=dimple_r, dimple_keep=dimple_keep);
    echo(btn_pocket_clr=btn_pocket_clr, btn_wall=btn_wall, btn_ceil_gap=btn_ceil_gap);
    echo(btn_bore_d=btn_bore_d, btn_ceil_z=btn_ceil_z, btn_flange_d=btn_flange_d);
    echo(btn_hook_ov=btn_hook_ov, btn_hook_slope=btn_hook_slope, btn_hook_u=btn_hook_u);
    echo(btn_hook_z0=btn_hook_z0, btn_tab_w=btn_tab_w, btn_tab_slit=btn_tab_slit);
    echo(btn_root_in=btn_root_in, btn_root_out=btn_root_out, mb_lead=mb_lead);
    echo(btn_in=btn_in, btn_out=btn_out, btn_skirt_x=btn_skirt_x);
    echo(btn_tab_flex=btn_tab_flex, btn_tab_len=btn_tab_len, btn_tab_strain=btn_tab_strain);
    echo(btn_tab_relief=btn_tab_relief, btn_tab_relief_clr=btn_tab_relief_clr, btn_root_z=btn_root_z);
    echo(btn_relief_x=btn_relief_x, btn_relief_y0=btn_relief_y0, btn_relief_y1=btn_relief_y1);
    echo(mic_seat_d=mic_seat_d, mic_seat_depth=mic_seat_depth);
    echo(hole_count=len([for (pt = hex_pts()) if (norm(pt) <= grille_r) 1]));
    echo(dimple_count=len([for (pt = hex_pts()) if (norm(pt) > grille_r && dimple_ok(pt)) 1]));
}
