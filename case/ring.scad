include <params.scad>
use <lib.scad>

ring_fn = 360;
ring_end_gap = 0.1;
ring_end_deg = 90 - bridge_half_deg - 180 / PI * ring_end_gap / filament_r;
ring_arc_h = 2.88;
ring_axis_z = ring_arc_h / 2;

ring_neck_w = 1.3;
ring_slit_in = ring_pocket_r * sqrt(2) / 2 + 0.005;
ring_slit_out = ring_neck_w - ring_slit_in;
ring_arc_steps = 36;

ring_lip_chamfer = 0.6;
ring_lip_out_r = body_inner_r(ring_z0 - ring_lip_h) - clr;
ring_lip_in_r = ring_lip_out_r - ring_lip_w;
ring_lip_lap = 0.01;

ring_front_wall = ring_outer_r - filament_r - ring_pocket_r;
ring_above_t = ring_axis_z - ring_pocket_r;
ring_below_t = ring_arc_h - ring_axis_z - ring_pocket_r;
ring_slit_th0 = asin(sqrt(ring_pocket_r * ring_pocket_r - ring_slit_out * ring_slit_out) / ring_pocket_r);
ring_slit_th1 = 180 - asin(sqrt(ring_pocket_r * ring_pocket_r - ring_slit_in * ring_slit_in) / ring_pocket_r);

function ring_pt(p) = [filament_r + p[0], ring_axis_z + p[1]];

function ring_groove_pts() = concat(
    [ring_pt([ring_slit_out, ring_arc_h])],
    [for (i = [0 : ring_arc_steps])
        let (th = ring_slit_th0 - i * (360 - (ring_slit_th1 - ring_slit_th0)) / ring_arc_steps)
            ring_pt([ring_pocket_r * cos(th), ring_pocket_r * sin(th)])],
    [ring_pt([-ring_slit_in, ring_arc_h])]
);

function ring_lip_pts() = [
    [ring_lip_in_r, ring_arc_h - ring_lip_lap],
    [ring_lip_out_r, ring_arc_h - ring_lip_lap],
    [ring_lip_out_r, ring_arc_h + ring_lip_h - ring_lip_chamfer],
    [ring_lip_out_r - ring_lip_chamfer, ring_arc_h + ring_lip_h],
    [ring_lip_in_r, ring_arc_h + ring_lip_h]
];

module ring_profile() {
    difference() {
        union() {
            translate([ring_in_r, 0]) square([ring_outer_r - ring_in_r, ring_arc_h]);
            polygon(ring_lip_pts());
        }
        polygon(ring_groove_pts());
    }
}

module ring_checks() {
    assert(ring_end_deg < 90 - bridge_half_deg);
    assert(abs(ring_lip_in_r - ring_in_r) < 0.01);
    assert(ring_in_r <= ring_lip_in_r + 1e-6);
    assert(filament_r - ring_slit_in - ring_in_r >= 1.0);
    assert(ring_lip_out_r < filament_r - ring_slit_in);
    assert(ring_lip_out_r < body_inner_r(ring_z0));
    assert(body_inner_r(ring_z0) < filament_r - ring_slit_in + 0.1);
    assert(ring_pocket_r > fil_d / 2);
    assert(ring_neck_w < fil_d);
    assert(ring_slit_in >= ring_pocket_r * sqrt(2) / 2);
    assert(ring_slit_out >= ring_pocket_r * sqrt(2) / 2);
    assert(ring_above_t >= 0.55 && ring_below_t >= 0.55);
    assert(abs(ring_front_wall - ring_front_t) < 1e-9);
    assert(ring_arc_h > 2 * ring_pocket_r + 1);
    assert(ring_arc_h <= ring_h && ring_h - ring_arc_h <= 0.15);
}

module ring_arc_flat() {
    ring_checks();
    rotate([0, 0, -ring_end_deg])
        rotate_extrude(angle = 2 * ring_end_deg, $fn = ring_fn) ring_profile();
}

module ring_arc() {
    translate([0, 0, ring_z0 + ring_arc_h]) rotate([180, 0, 0]) ring_arc_flat();
}

module print_ring_arc() {
    ring_arc_flat();
}

module asm_ring() {
    for (b = bridge_angles) rotate([0, 0, b + 90]) ring_arc();
}

module echo_ring() {
    ring_checks();
    echo(ring_in_r=ring_in_r, ring_end_deg=ring_end_deg, ring_end_gap=ring_end_gap);
    echo(ring_pocket_r=ring_pocket_r, ring_neck_w=ring_neck_w, ring_slit_in=ring_slit_in, ring_slit_out=ring_slit_out);
    echo(ring_lip_h=ring_lip_h, ring_lip_w=ring_lip_w, ring_arc_h=ring_arc_h, ring_axis_z=ring_axis_z);
    echo(ring_lip_in_r=ring_lip_in_r, ring_lip_out_r=ring_lip_out_r);
    echo(ring_front_wall=ring_front_wall, ring_above_t=ring_above_t, ring_below_t=ring_below_t);
}
