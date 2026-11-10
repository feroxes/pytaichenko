clr = 0.2;
bed = 256;
board_clr = 0.45;

case_d = 84;
case_r = case_d / 2;
wall_t = 3.0;
wall_thin_t = 1.5;
inner_r = case_r - wall_t;
part_r = inner_r - clr;

function ceil1(x) = ceil(x * 10 - 1e-6) / 10;

insert_d = 3.3;
insert_len = 4;
insert_od = 3.5;
insert_hole_depth = insert_len + 1;
boss_od = 7.4;
boss_wall = (boss_od - insert_d) / 2;
insert_tip_gap = 0.2;
min_thread = 3.0;
screw_len = 8;
screw_d = 2.5;
screw_thru_d = 2.9;
screw_head_d = 4.7;
screw_head_h = 1.5;
screw_rim_h = screw_head_h - (screw_head_d - screw_d) / 2;
csk_clr = 0.1;
csk_gap = 0.03;
csk_d = screw_head_d + 2 * csk_clr;
csk_bore_h = screw_rim_h;
csk_h = csk_bore_h + (csk_d - screw_thru_d) / 2;
csk_head_z = csk_clr - csk_gap;

function csk_stand(d) = screw_rim_h + (screw_head_d - d) / 2;

latch_k = 1.25;
latch_vg = 0.1;
latch_relief = 2.3;
latch_root_fillet = 0.6;
latch_relief_clr = 0.2;
latch_side_clr = 0.4;

function latch_zt(board_top, ov, vg = latch_vg) = board_top + vg - latch_k * clr + latch_k * (ov + clr);

function latch_strain(board_top, ov, t, floor_z, c = clr, relief = latch_relief, vg = latch_vg) =
    1.5 * t * (ov + c) / pow(latch_zt(board_top, ov, vg) - (floor_z - relief), 2);

tray_skin_t = 1.0;
tray_floor_t = 1.5;
wire_clear_h = 5;

dk_len = 63.2;
dk_ant_out = 6;
dk_ant_w = 17.9;
dk_ant_t = 0.8;
dk_board_len = dk_len - dk_ant_out;
dk_w = 28.2;
dk_t = 1.6;
dk_row_pitch = 25.4;
dk_pins = 22;
dk_pin_pitch = 2.54;
dk_pin1_from_usb = 1.5;
dk_pin_sq = 0.64;
dk_hdr_w = 2.54;
dk_hdr_h = 3;
dk_pin_short = 2.5;
dk_pin_long = 4;
dk_usb_w = 8.88;
dk_usb_h = 3.1;
dk_usb_top = 3.2;
dk_usb_pitch = 11.5;
dk_usb_out = 0.5;
dk_usb_len = 7.5;
dk_com_side = 1;
dk_com_x = dk_com_side * dk_usb_pitch / 2;
dk_usb2_x = -dk_com_x;
dk_mod_w = 18;
dk_mod_l = 19.5;
dk_mod_h = 3.1;
dk_nest_wall = 1.2;

dk_hdr_z0 = tray_skin_t + dk_pin_short;
dk_z0 = dk_hdr_z0 + dk_hdr_h;
dk_z1 = dk_z0 + dk_t;
dk_pin_top_z = dk_z1 + dk_pin_long;
dk_pin_bot_z = dk_hdr_z0 - dk_pin_short;
dk_y0 = -floor(100 * sqrt(part_r * part_r - pow(dk_w / 2, 2))) / 100;
dk_y1 = dk_y0 + dk_board_len;
dk_ant_y1 = dk_y1 + dk_ant_out;
dk_pin_y0 = dk_y0 + dk_pin1_from_usb;
dk_pin_y1 = dk_pin_y0 + (dk_pins - 1) * dk_pin_pitch;
dk_row_x = dk_row_pitch / 2;
antenna_top_z = dk_z1 + dk_ant_t;

usb_face_y = dk_y0 - dk_usb_out;
usb_face_gap = case_r + usb_face_y;
usb_face_gap_max = 5.4;
usb_z = dk_z1 + dk_usb_top - dk_usb_h / 2;
usb_plug_w = 12;
usb_plug_h = 7;
usb_plug_len = 14;
usb_shell_w = 8.4;
usb_shell_h = 2.6;
usb_shell_l = 6.65;
usb_plug_seat = 0.7;
usb_cav_clr = 0.01;
usb_plug_face_y = usb_face_y - usb_plug_seat;
usb_shell_tip_y = usb_plug_face_y + usb_shell_l;

spk_flange_d = 50.3;
spk_flange_t = 0.7;
spk_open_d = 46;
spk_rim_w = 1.75;
spk_rim_h = 1.3;
spk_ear_tip = 68;
spk_ear_pitch = 60;
spk_ear_hole_d = 4;
spk_ear_tip_r = (spk_ear_tip - spk_ear_pitch) / 2;
spk_ear_base_x = 15;
spk_ear_base_w = 26;
spk_depth = 35.3;
spk_basket_d = 45.4;
spk_magnet_d = 44.65;
spk_magnet_h = 17;
spk_cap_rise = 2;
spk_grille_gap = 4;
spk_term_z = 14;
spk_term_out = 4;
spk_term_in = 2;
spk_term_w = 18;
spk_term_h = 10;
spk_term_tip = 3;
spk_gasket_t = 1.2;
spk_gasket_squeeze = 0.3;
spk_gasket_c = spk_gasket_t - spk_gasket_squeeze;
spk_ear_standoff_h = spk_rim_h + spk_gasket_c;
spk_mag_gap = 2;
grille_t = 2.0;

spk_mag_z = dk_pin_top_z + wire_clear_h + spk_mag_gap;
spk_magnet_top_z = spk_mag_z + spk_magnet_h;
spk_rim_top_z = spk_mag_z + spk_depth;
spk_ear_z = spk_rim_top_z - spk_rim_h;
spk_flange_bot_z = spk_ear_z - spk_flange_t;
spk_cap_top_z = spk_rim_top_z + spk_cap_rise;
spk_term_z0 = spk_ear_z - spk_term_z - spk_term_tip;
spk_term_z1 = spk_term_z0 + spk_term_h;
spk_lid_z = spk_rim_top_z + spk_gasket_c;
grille_bot_z = spk_rim_top_z + spk_grille_gap;
H = grille_bot_z + grille_t;
antenna_metal_gap = spk_mag_z - antenna_top_z;

amp_w = 18.7;
amp_l = 17.7;
amp_t = 1.6;
amp_total_h = 10.4;
amp_block_h = amp_total_h - amp_t;
amp_hdr_n = 7;
amp_hdr_pitch = 2.54;
amp_hdr_w = 2.54;
amp_hdr_h = 2.5;
amp_hdr_pin = 6;
amp_hdr_pin_d = 0.64;
amp_hdr_in = 0.3;
amp_hdr_tail = 1.4;
amp_block_w = 7.5;
amp_block_l = 10;
amp_clr = board_clr;
amp_pad_h = 2.0;
amp_z0 = tray_floor_t + amp_pad_h;
amp_x0 = dk_w / 2 + board_clr + dk_nest_wall + amp_clr;
tray_amp_stop_t = 1.2;
amp_rot = -90;
amp_y = 4.5;
amp_xy = [amp_x0 + amp_l / 2, amp_y];
amp_hdr_lx = amp_w / 2 - amp_hdr_in - amp_hdr_w / 2;
function amp_to_world(q) = amp_xy + [q[0] * cos(amp_rot) - q[1] * sin(amp_rot), q[0] * sin(amp_rot) + q[1] * cos(amp_rot)];
amp_hdr_xy = [for (k = [0 : amp_hdr_n - 1]) amp_to_world([amp_hdr_lx, (k - (amp_hdr_n - 1) / 2) * amp_hdr_pitch])];
amp_pin_top_z = amp_z0 + amp_t + amp_hdr_h + amp_hdr_pin;
amp_block_top_z = amp_z0 + amp_total_h;
amp_top_z = max(amp_pin_top_z, amp_block_top_z);

cap_d = 10;
cap_l = 17;
cap_gap = 0.6;
cap_axis_y = amp_y - amp_w / 2 - amp_clr - tray_amp_stop_t - cap_gap - cap_d / 2;
cap_xy = [amp_x0 + cap_l / 2, cap_axis_y];
cap_axis_z = tray_floor_t + cap_d / 2;

mic_d = 13.8;
mic_t = 1.0;
mic_total_t = 2.0;
mic_can_h = mic_total_t - mic_t;
mic_can_w = 4.7;
mic_can_l = 3.8;
mic_port_d = 1.8;
mic_angle = 55;
mic_z = 24;
mic_az_r = 90 - mic_angle;
mic_az_l = 90 + mic_angle;
mic_pocket_clr = 0.35;
mic_pocket_r = mic_d / 2 + mic_pocket_clr;
mic_rim_w = 0.8;
mic_fill_r = mic_pocket_r + mic_rim_w;
mic_open_r = sqrt(inner_r * inner_r - mic_fill_r * mic_fill_r);
mic_recess = mic_t;
mic_face_r = mic_open_r + mic_recess;
mic_port_out_d = 3.2;
mic_port_flare = 1.2;
mic_board_r = mic_face_r - mic_t / 2;
mic_hole_chord = 2 * case_r * sin(mic_angle);
mic_board_chord = 2 * mic_board_r * sin(mic_angle);

btn_w = 11.9;
btn_body_h = 3.0;
btn_h = 5.7;
btn_pusher_d = 6.5;
btn_tip_gap = 0.5;
btn_leg_x = 5.5;
btn_leg_y = 2.5;
btn_leg_w = 1.0;
btn_leg_t = 0.5;
btn_leg_l = 3.5;
btn_stub_l = 0.5;
btn_leg_gap = 0.5;
button_hole_d = 9;
top_plate_t = 3.0;
btn_pocket_clr = 0.15;
btn_wall = 1.0;
btn_in = btn_w / 2 + btn_pocket_clr;
btn_out = btn_in + btn_wall;
btn_flange_gap = 0.5;
button_az = -90;
button_r = spk_flange_d / 2 + btn_flange_gap + btn_out;
btn_corner_r = norm([button_r + btn_out, btn_out]);
btn_tip_z = H - top_plate_t - btn_tip_gap;
btn_z0 = btn_tip_z - btn_h;
top_region_h = top_plate_t + btn_tip_gap + btn_h + btn_stub_l + btn_leg_gap;
top_z0 = H - top_region_h;

ring_h = 3.0;
ring_front_t = 0.6;
fil_lit = 175;
fil_contact = 5;
fil_d = 1.5;
fil_bend_r_min = 5;
ring_pocket_r = (fil_d + clr) / 2;
filament_r = case_r - ring_front_t - ring_pocket_r;
ring_z0 = top_z0 - ring_h;
ring_z1 = top_z0;
body_bot_z = 14.9;
body_layer = 0.16;
first_layer = 0.2;

function printed_top(h, layer, first = first_layer) = first + layer * max(0, ceil((h - first) / layer - 0.5));

ring_arc_h = round(100 * (printed_top(ring_z1 - body_bot_z, body_layer) - printed_top(ring_z0 - body_bot_z, body_layer))) / 100;
fil_z = ring_z0 + ring_arc_h / 2;
fil_circ = 2 * PI * filament_r;
fil_gap_len = fil_circ - fil_lit;
fil_lit_deg = 360 * fil_lit / fil_circ;
btn_pocket_arc = 2 * atan2(btn_out, button_r - btn_out) * PI / 180 * filament_r;

fil_trans = 3;
fil_bend_r = 5.5;
fil_turn = 35;
fil_bend_steps = 7;
chan_d = 1.7;
fil_bend_len = fil_bend_r * fil_turn * PI / 180;
fil_tail_lit = fil_bend_len + fil_trans;
fil_straight = fil_trans + fil_contact;
fil_ring_len = fil_lit - 2 * fil_tail_lit;
fil_ring_deg = 360 * fil_ring_len / fil_circ;
fil_ring_half = fil_ring_deg / 2;
fil_end_az = 90 + fil_ring_half;
fil_er = [cos(fil_end_az), sin(fil_end_az)];
fil_et = [-sin(fil_end_az), cos(fil_end_az)];
fil_bend_c = (filament_r - fil_bend_r) * fil_er;
fil_bend_end = fil_bend_c + fil_bend_r * (cos(fil_turn) * fil_er + sin(fil_turn) * fil_et);
fil_tail_dir = -sin(fil_turn) * fil_er + cos(fil_turn) * fil_et;
fil_tail_pts = concat(
    [for (i = [0 : fil_bend_steps])
        let (a = fil_turn * i / fil_bend_steps, q = fil_bend_c + fil_bend_r * (cos(a) * fil_er + sin(a) * fil_et))
            [q[0], q[1], fil_z]],
    [for (s = [fil_trans, fil_straight]) let (q = fil_bend_end + s * fil_tail_dir) [q[0], q[1], fil_z]]
);
fil_tail_pts_m = [for (q = fil_tail_pts) [-q[0], q[1], q[2]]];
fil_tail_end = fil_tail_pts[len(fil_tail_pts) - 1];
fil_contact_start = fil_tail_pts[len(fil_tail_pts) - 2];

tr_w = 10.16;
tr_body_l = 9.15;
tr_body_h = 4.45;
tr_tab_l = 6.35;
tr_tab_t = 1.3;
tr_lead_l = 4.5;
tr_lead_w = 0.8;
tr_lead_t = 0.5;
tr_lead_z = 2.0;
tr_lead_pitch = 2.54;
tr_len = tr_tab_l + tr_body_l + tr_lead_l;
tr_zone = [14, 24];
transistor_xy = [-(dk_w / 2 + board_clr + dk_nest_wall) - tr_zone[0] / 2, 0];
tr_z0 = tray_floor_t;

washer_id = 2.7;
washer_od = 6.0;
washer_t = 0.5;

spk_ear_xy = [[-spk_ear_pitch / 2, 0], [spk_ear_pitch / 2, 0]];
spk_standoff_z0 = spk_lid_z - spk_ear_standoff_h;
spk_washer_bot_z = spk_flange_bot_z - washer_t;
spk_screw_head_z = spk_washer_bot_z - csk_stand(washer_id);
spk_screw_tip_z = spk_screw_head_z + screw_len;
spk_screw_engage = spk_screw_tip_z - spk_standoff_z0;
spk_screw_thread = min(insert_len, spk_screw_engage);
spk_insert_hole_depth = max(insert_hole_depth, ceil1(spk_screw_tip_z + insert_tip_gap - spk_standoff_z0));
spk_insert_top_z = spk_standoff_z0 + spk_insert_hole_depth;
spk_boss_skin = H - spk_insert_top_z;

spk_gasket_id = spk_flange_d - 2 * spk_rim_w;
spk_gasket_od = spk_flange_d;
spk_gasket_z0 = spk_rim_top_z;
spk_recess_r = spk_gasket_id / 2;
spk_lip_in_r = spk_gasket_od / 2 + clr;
spk_lip_w = 1.0;
spk_lip_drop = 0.4;
spk_lip_z0 = spk_rim_top_z - spk_lip_drop;

top_under_z = spk_lid_z;
top_t = H - top_under_z;
top_skirt_in_r = inner_r + 0.3;
top_edge_r = 8.5;
top_edge_psi = 40;
top_bevel_l = 1.2;
top_edge_z0 = H - top_edge_r * sin(top_edge_psi) - top_bevel_l * cos(top_edge_psi);
top_face_r = case_r - top_edge_r + top_edge_r * cos(top_edge_psi) - top_bevel_l * sin(top_edge_psi);

function top_outer_r(z) = z >= H - top_bevel_l * cos(top_edge_psi)
    ? top_face_r + (H - z) * tan(top_edge_psi)
    : z <= top_edge_z0 ? case_r
    : case_r - top_edge_r + sqrt(top_edge_r * top_edge_r - pow(z - top_edge_z0, 2));

grille_margin = 0.5;

top_screw_r = 33;
top_screw_angles = [90, 210, 330];
top_screw_xy = [for (a = top_screw_angles) [top_screw_r * cos(a), top_screw_r * sin(a)]];
top_plug_d = 6.4;
top_plug_depth = 1.2;
top_plug_h = 1.0;
top_plug_fit = 0.04;
top_plug_chamfer = 0.25;
top_plug_clr = 0.15;
top_plug_ribs = 8;
top_plug_rib_w = 0.6;
top_plug_edge = 0.15;
top_plug_grip_h = top_plug_h - top_plug_chamfer - top_plug_edge;
top_plug_rim = 0.8;
top_seat_z = H - top_plug_depth;
top_screw_head_z = top_seat_z - csk_head_z;
top_screw_tip_z = top_screw_head_z - screw_len;
top_seat_floor_t = 2.0;
top_boss_z0 = top_seat_z - csk_h - top_seat_floor_t;
top_boss_gap = 0.3;
top_col_z1 = top_boss_z0 - top_boss_gap;
top_sock_d = boss_od + 2 * clr;
top_screw_engage = top_col_z1 - top_screw_tip_z;
top_screw_thread = min(insert_len, top_screw_engage);
top_insert_hole_depth = max(insert_hole_depth, ceil1(top_screw_engage + insert_tip_gap));
top_insert_bot_z = top_col_z1 - top_insert_hole_depth;

btn_plate_z = H - top_plate_t;
btn_ceil_gap = 0.5;
btn_ceil_z = btn_z0 + btn_body_h + btn_ceil_gap;
btn_bore_d = 10.9;
btn_flange_d = button_hole_d - 2 * clr + 1.8;
btn_rim_min = 0.8;
btn_hook_ov = 0.6;
btn_hook_slope = 1;
btn_hook_u = btn_in - (btn_w / 2 - btn_hook_ov);
btn_hook_z0 = btn_z0 - btn_hook_u * btn_hook_slope;
btn_tab_w = 5;
btn_tab_slit = 0.6;
btn_tab_cut_in = 2.2;
btn_root_in = 0.6;
btn_root_out = 2.0;
btn_tab_t = 0.8;
btn_tab_flex = 0.7;
btn_tab_relief = 2.0;
btn_tab_relief_clr = 0.2;
btn_root_z = btn_plate_z + btn_tab_relief;
btn_tab_len = btn_root_z - btn_z0;
btn_relief_x = btn_tab_w / 2 + btn_tab_slit;
btn_relief_y0 = btn_in - btn_root_in - btn_tab_relief_clr;
btn_relief_y1 = btn_in + btn_tab_t + btn_root_out + btn_tab_relief_clr;
btn_relief_z0 = top_under_z - 0.5;
btn_tab_strain = 1.5 * btn_tab_t * btn_tab_flex / (btn_tab_len * btn_tab_len);
btn_skirt_x = max(btn_in, top_skirt_in_r + 1 - button_r);

face_thru_r = spk_recess_r - grille_margin;
face_min_hole_d = 2.2;
face_min_web = 1.2;
face_edge_land = 0.8;

sf_golden = 180 * (3 - sqrt(5));
sf_spin = 0;
sf_dimple_depth = 0.8;
sf_dimple_wall = 55;
sf_plug_clear = 1.2;
sf_btn_clear = 1.2;
sf_relief_clear = 0.6;
sf_dimple_floor_shrink = 2 * sf_dimple_depth / tan(sf_dimple_wall);

function rect_dist(px, py, x0, x1, y0, y1) = norm([max(x0 - px, 0, px - x1), max(y0 - py, 0, py - y1)]);

function button_local(p) = let (a = -button_az, q = [p[0], p[1]] - [button_r * cos(button_az), button_r * sin(button_az)])
    [q[0] * cos(a) - q[1] * sin(a), q[0] * sin(a) + q[1] * cos(a)];

function face_feature_clear(p, d) = let (b = button_local(p)) min(concat(
    [top_face_r - face_edge_land - norm([p[0], p[1]]) - d / 2],
    [norm(b) - d / 2 - button_hole_d / 2 - sf_btn_clear],
    [for (s = [-1, 1]) rect_dist(b[0], b[1], -btn_relief_x, btn_relief_x,
        s > 0 ? btn_relief_y0 : -btn_relief_y1, s > 0 ? btn_relief_y1 : -btn_relief_y0) - d / 2 - sf_relief_clear],
    [for (c = top_screw_xy) norm([p[0], p[1]] - c) - d / 2 - top_plug_d / 2 - sf_plug_clear]
));

function sum_sq(v, i = 0) = i >= len(v) ? 0 : v[i] * v[i] + sum_sq(v, i + 1);

function pts_min_web(v) = let (n = len(v)) min([for (i = [0 : n - 2], j = [i + 1 : n - 1])
    norm([v[i][0], v[i][1]] - [v[j][0], v[j][1]]) - (v[i][2] + v[j][2]) / 2]);

sf_c = 3.1;
sf_n0 = 1.5;
sf_d = 3.8;
sf_thru_shrink = 0.6;
sf_dimple_d0 = 3.4;
sf_dimple_d1 = 2.4;
sf_n_max = ceil(pow(top_face_r / sf_c, 2));

function sf_dimple_d(r) = sf_dimple_d0 - (sf_dimple_d0 - sf_dimple_d1) * max(0, min(1, (r - face_thru_r) / (top_face_r - face_thru_r)));

function sf_raw() = [
    for (n = [0 : sf_n_max])
        let (r = sf_c * sqrt(n + sf_n0), a = n * sf_golden + sf_spin, dt = min(sf_d, 2 * (face_thru_r - r)),
             thru = dt >= max(face_min_hole_d, sf_d - sf_thru_shrink))
            if (r <= top_face_r) [r * cos(a), r * sin(a), thru ? dt : sf_dimple_d(r), thru ? 1 : 0]
];

sf_pts = [for (p = sf_raw()) if (p[3] == 1 || face_feature_clear(p, p[2]) >= 0) p];
sf_thru_pts = [for (p = sf_pts) if (p[3] == 1) p];
sf_thru_n = len(sf_thru_pts);
sf_dimple_n = len(sf_pts) - sf_thru_n;
sf_min_web = pts_min_web(sf_pts);
sf_thru_reach = max([for (p = sf_thru_pts) norm([p[0], p[1]]) + p[2] / 2]);
sf_open_ratio = sum_sq([for (p = sf_thru_pts) p[2]]) / (spk_open_d * spk_open_d);
sf_dimple_floor_max = max([for (p = sf_pts) if (p[3] == 0) p[2]]) - sf_dimple_floor_shrink;

chamfer_slope = 1.05;

ring_lip_h = 3;
ring_lip_w = 1.2;
ring_lip_clr = 0.2;
ring_lip_chamfer = 0.6;
ring_lip_lap = 0.01;
ring_in_clr = 0.5;
ring_end_gap = 0.1;
ring_neck_w = 1.3;
ring_outer_r = case_r;
ring_lip_out_r = inner_r - ring_lip_clr;
ring_in_r = ring_lip_out_r - ring_lip_w;
ring_end_deg = 180 / PI * ring_end_gap / filament_r;
ring_slit_in = ring_pocket_r * sqrt(2) / 2 + 0.005;
ring_slit_out = ring_neck_w - ring_slit_in;
ring_axis_h = ring_arc_h / 2;
ring_front_wall = ring_outer_r - filament_r - ring_pocket_r;
ring_above_t = ring_axis_h - ring_pocket_r;
ring_below_t = ring_arc_h - ring_axis_h - ring_pocket_r;
ring_top_z = ring_z0 + ring_arc_h;
bridge_a0 = fil_end_az + ring_end_deg;
bridge_a1 = 540 - fil_end_az - ring_end_deg;

top_ledge_h = 3.2;
top_ledge_z1 = ring_z0 - ring_lip_h - ring_lip_clr;
top_ledge_z0 = top_ledge_z1 - top_ledge_h;
col_bar_w = 4;
col_wedge_r = inner_r + wall_t / 2;
col_corbel_z0 = top_ledge_z0 - chamfer_slope * (col_wedge_r - (top_screw_r - boss_od / 2));
fil_col_clr = min([for (c = top_screw_xy, q = concat(
        [for (i = [0 : 20]) let (a = fil_turn * i / 20) fil_bend_c + fil_bend_r * (cos(a) * fil_er + sin(a) * fil_et)],
        [for (i = [0 : 40]) fil_bend_end + fil_straight * i / 40 * fil_tail_dir]),
        m = [-1, 1]) norm([m * q[0], q[1]] - c)]) - boss_od / 2 - chan_d / 2;

tray_screw_r = 37.5;
tray_screw_angles = [55, 125, 230, 310];
tray_screw_xy = [for (a = tray_screw_angles) [tray_screw_r * cos(a), tray_screw_r * sin(a)]];
tray_insert_hole_depth = insert_hole_depth;
tray_insert_top_z = body_bot_z + tray_insert_hole_depth;
tray_tip_h = insert_d / 2 * chamfer_slope;
tray_boss_skin = 1.2;
tray_boss_top_z = tray_insert_top_z + tray_tip_h + tray_boss_skin;

top_gasket_t = 1.2;
top_gasket_squeeze = 0.3;
top_gasket_c = top_gasket_t - top_gasket_squeeze;
top_gasket_w = 1.2;
top_gasket_fit = -0.2;
top_groove_wall = 0.55;
top_groove_out_r = case_r - top_groove_wall;
top_groove_in_r = top_skirt_in_r + top_groove_wall;
top_groove_depth = top_gasket_c;
top_gasket_od = 2 * (top_groove_out_r + top_gasket_fit);
top_gasket_id = top_gasket_od - 2 * top_gasket_w;

wire_probe_d = 4;
wire_probe_r = top_screw_r;
wire_probe_z1 = 15;
wire_btn_pts = [[0, -(button_r - btn_leg_x) - 2.25, btn_z0 - btn_leg_l - 1.2],
    [0, -wire_probe_r, spk_term_z0 + 4], [0, -wire_probe_r, wire_probe_z1]];
wire_term_pts = [[0, -(spk_basket_d / 2 + spk_term_out) - 2.5, (spk_term_z0 + spk_term_z1) / 2],
    [0, -wire_probe_r, spk_term_z0], [0, -wire_probe_r, wire_probe_z1]];

tray_slot_w = dk_hdr_w + 2 * board_clr;
tray_slot_y0 = dk_pin_y0 - dk_pin_pitch / 2 - board_clr;
dkf_stop_clr = 0.1;
tray_slot_y1 = dk_pin_y1 + dk_pin_pitch / 2 + dkf_stop_clr;
tray_pin_gap = 0.3;
tray_pin_floor_z = dk_pin_bot_z - tray_pin_gap;
tray_pin_groove_w = dk_pin_sq + 2 * board_clr + 0.2;
tray_pin_groove_y0 = dk_pin_y0 - tray_pin_groove_w / 2;
tray_pin_groove_y1 = dk_pin_y1 + tray_pin_groove_w / 2;
tray_rail_in_x = dk_row_x - tray_slot_w / 2 - dk_nest_wall;
tray_rail_out_x = amp_x0 - amp_clr;
tray_rail_top_z = dk_z0 - 0.5;
dkf_latch_c = 0.35;
tray_rail_y1 = dk_y1 + dkf_latch_c - latch_side_clr;
dkf_stop_t = tray_rail_y1 - tray_slot_y1;
dk_back_travel = min(board_clr, sqrt(inner_r * inner_r - pow(dk_w / 2, 2)) + dk_y0);

tray_latch_relief = 0.5;
tray_latch_root_z = tray_floor_t - tray_latch_relief;
dks_latch_t = 0.8;
dks_latch_ov = 0.5;
dks_latch_c = clr;
dks_latch_w = 5;
dks_latch_y = -27;
dkf_latch_t = 0.65;
dkf_rail_cut = dk_y1 - tray_rail_y1;
dkf_latch_ov = 0.5;
dkf_latch_fillet = 1.0;
dkf_latch_vg = 0.2;
dkf_hook_touch = dkf_latch_vg / latch_k;
dkf_latch_x0 = dk_mod_w / 2 + 0.4;
dkf_latch_x1 = dk_w / 2 - 0.1;
dks_latch_strain = latch_strain(dk_z1, dks_latch_ov, dks_latch_t, tray_floor_t, dks_latch_c, tray_latch_relief);
dkf_latch_strain = latch_strain(dk_z1, dkf_latch_ov, dkf_latch_t, tray_floor_t, dkf_latch_c, tray_latch_relief, dkf_latch_vg);

tray_amp_pad_lx1 = amp_hdr_lx - amp_hdr_pin_d / 2 - 0.6;
tray_amp_bar_lx0 = amp_hdr_lx + amp_hdr_pin_d / 2 + 0.45;
tray_amp_stop_top_z = amp_z0 + amp_t + 0.6;
amp_latch_t = 0.6;
amp_latch_ov = 0.4;
amp_latch_c = clr;
amp_latch_w = 6;
amp_latch_lx = 0;
amp_lip_w = 8;
amp_lip_lx = -1;
amp_lip_gap = 0.1;
amp_lip_ov = 0.6;
amp_lip_z = amp_z0 + amp_t + latch_vg;
amp_latch_strain = latch_strain(amp_z0 + amp_t, amp_latch_ov, amp_latch_t, tray_floor_t, amp_latch_c, tray_latch_relief);

tray_com_w = usb_plug_w + 2 * clr;
tray_com_h = usb_plug_h + 2 * clr;

com_plug_w = 10;
com_plug_h = 6.2;
com_plug_clr = 0.1;
com_bush_fit = 0.15;
com_bush_in = 1.0;
com_bush_min_wall = 0.4;
com_hole_w = com_plug_w + 2 * com_plug_clr;
com_hole_h = com_plug_h + 2 * com_plug_clr;
com_bush_y1 = -(inner_r - com_bush_in);
com_slot_far_y = -sqrt(inner_r * inner_r - pow(abs(dk_com_x) + tray_com_w / 2, 2));

tray_screw_tip_z = tray_insert_top_z - insert_tip_gap;
tray_screw_head_z = tray_screw_tip_z - screw_len;
tray_seat_z = tray_screw_head_z - csk_head_z;
tray_screw_engage = tray_screw_tip_z - body_bot_z;
tray_screw_thread = min(insert_len, tray_screw_engage);
tray_access_d = 6.0;
tray_col_wall = 1.2;
tray_col_od = tray_access_d + 2 * tray_col_wall;
tray_col_drop = 0.02;
tray_access_z1 = tray_seat_z - (tray_access_d - csk_d) / 2;

tray_gasket_t = 1.2;
tray_gasket_squeeze = 0.3;
tray_gasket_c = tray_gasket_t - tray_gasket_squeeze;
tray_gasket_w = 1.2;
tray_gasket_fit = -0.2;
tray_groove_wall = 0.7;
tray_groove_out_r = case_r - tray_groove_wall;
tray_groove_in_r = inner_r + tray_groove_wall;
tray_groove_depth = tray_gasket_c;
tray_gasket_od = 2 * (tray_groove_out_r + tray_gasket_fit);
tray_gasket_id = tray_gasket_od - 2 * tray_gasket_w;
tray_com_web = body_bot_z - tray_groove_depth - (usb_z + tray_com_h / 2);

foot_w = 10;
foot_l = 6;
foot_corner_r = 2;
foot_niche_skin = 0.8;
foot_niche_h = 1.8;
foot_out = 2;
foot_h = foot_niche_h + foot_out;
foot_niche_clr = 0.3;
foot_pad_grow = 2.2;
foot_pad_h = foot_niche_h + foot_niche_skin;
foot_r = 35.5;
foot_a4 = 25;
foot_xy = [[0, foot_r], [-foot_r, 0], [0, -foot_r], [foot_r * cos(foot_a4), foot_r * sin(foot_a4)]];
foot_rot = [90, 180, 270, foot_a4];

tray_wire_end_y = dk_pin_y0 + 3 * dk_pin_pitch;
tray_wire_pts = [wire_btn_pts[len(wire_btn_pts) - 1],
    [dk_row_x, tray_wire_end_y, dk_pin_top_z + wire_probe_d / 2 + 0.1],
    [-dk_row_x, tray_wire_end_y, dk_pin_top_z + wire_probe_d / 2 + 0.1]];

assert(spk_ear_tip / 2 + clr <= inner_r);
assert(button_r - btn_out >= spk_flange_d / 2 + 0.5 - 1e-9);
assert(btn_corner_r <= case_r - wall_thin_t + 1e-6);
assert(len([for (sx = [-1, 1], y = [dk_y0, dk_y1]) if (norm([sx * dk_w / 2, y]) > part_r + 1e-6) 1]) == 0);
assert(norm([dk_ant_w / 2, dk_ant_y1]) <= part_r);
assert(usb_face_gap <= usb_face_gap_max);
assert(dk_pin_y1 + dk_pin_pitch / 2 <= dk_y1);
assert(dk_row_x + dk_hdr_w / 2 <= dk_w / 2);
assert(dk_com_x + dk_usb_w / 2 < dk_row_x - dk_hdr_w / 2);
assert(dk_mod_w / 2 < dk_row_x - dk_hdr_w / 2);
assert(dk_mod_h < dk_usb_top);
assert(usb_shell_w < dk_usb_w && usb_shell_h < dk_usb_h && usb_shell_l < dk_usb_len);
assert(usb_shell_tip_y > usb_face_y);
assert(mic_hole_chord >= 60);
assert(mic_board_chord >= 60);
assert(H >= 55 && H <= 62);
assert(spk_mag_z >= dk_pin_top_z + wire_clear_h - 1e-9);
assert(amp_top_z + wire_clear_h <= spk_mag_z + 1e-9);
assert(antenna_metal_gap >= 10);
assert(grille_bot_z - spk_cap_top_z >= 2);
assert(fil_lit < fil_circ);
assert(fil_gap_len >= btn_pocket_arc);
assert(spk_basket_d / 2 + spk_term_out < button_r - btn_leg_x - btn_leg_t / 2);
assert(spk_term_z1 < btn_z0 - btn_leg_l);
assert(len([for (sx = [-1, 1], sy = [-1, 1])
    if (norm([amp_xy[0] + sx * (amp_w / 2 + amp_clr), amp_xy[1] + sy * (amp_l / 2 + amp_clr)]) > part_r) 1]) == 0);
assert(cap_axis_z + cap_d / 2 + wire_clear_h <= spk_mag_z);
assert(norm([cap_xy[0] + cap_l / 2, cap_axis_y - cap_d / 2]) <= part_r - 0.5 && norm([cap_xy[0] + cap_l / 2 + 1.5, cap_axis_y - cap_d / 4]) <= part_r - 0.5);
assert(min([for (q = tray_screw_xy) norm([max(cap_xy[0] - cap_l / 2 - q[0], 0, q[0] - cap_xy[0] - cap_l / 2),
    max(cap_axis_y - cap_d / 2 - q[1], 0, q[1] - cap_axis_y - cap_d / 2)])]) >= tray_col_od / 2 + 1.0);
assert(len([for (sx = [-1, 1], sy = [-1, 1])
    if (norm(transistor_xy + [sx * tr_zone[0] / 2, sy * tr_zone[1] / 2]) > part_r) 1]) == 0);
assert(tr_w <= tr_zone[0] && tr_len <= tr_zone[1]);
assert(tr_zone[0] >= 12 && tr_zone[1] >= 20);
assert(mic_z - mic_d / 2 > amp_top_z);
assert(top_z0 > spk_term_z1 && ring_z1 <= btn_z0);
assert(washer_id > screw_d && washer_id < screw_head_d);
assert(washer_od / 2 <= spk_ear_tip_r);
assert(csk_stand(washer_id) > screw_rim_h && csk_stand(washer_id) < screw_head_h);
assert(abs(spk_standoff_z0 - spk_ear_z) < 1e-9);
assert(boss_od / 2 <= spk_ear_tip_r);
assert(spk_ear_pitch / 2 - boss_od / 2 > spk_flange_d / 2 + 0.5);
assert(spk_screw_tip_z + insert_tip_gap <= spk_insert_top_z + 1e-9);
assert(spk_screw_thread >= min_thread && spk_screw_engage >= insert_len);
assert(spk_insert_hole_depth >= insert_len + 1);
assert(spk_boss_skin >= 1.6);
assert(spk_recess_r >= spk_open_d / 2 + 0.3);
assert(spk_gasket_squeeze / spk_gasket_t >= 0.2 && spk_gasket_squeeze / spk_gasket_t <= 0.3);
assert(spk_lip_z0 > spk_ear_z + 0.5);
assert(spk_lip_in_r + spk_lip_w < spk_ear_pitch / 2);
assert(sf_dimple_depth < top_t - 1.0 && sf_dimple_depth < grille_t - 1.0 && spk_boss_skin - sf_dimple_depth >= 0.8);
assert(sf_min_web >= face_min_web && min([for (p = sf_pts) p[2]]) >= face_min_hole_d);
assert(sf_thru_reach <= face_thru_r + 1e-9 && sf_open_ratio >= 0.30);
assert(sf_dimple_floor_max > 0 && sf_dimple_floor_max <= 2.5);
assert(top_edge_z0 > top_z0 + 1 && top_edge_z0 - ring_z1 <= 4);
assert(top_skirt_in_r < case_r - 2);
assert(top_screw_r - boss_od / 2 - spk_flange_d / 2 >= 2.5);
assert(top_screw_r + top_sock_d / 2 <= top_skirt_in_r - 1);
assert(top_screw_r + top_plug_d / 2 + 1.0 <= top_face_r);
assert(top_plug_d >= csk_d + 1.0);
assert(top_plug_fit >= 0 && top_plug_fit <= 0.2);
assert(top_plug_h <= top_plug_depth - 0.15);
assert(top_plug_clr >= 0.05 && top_plug_grip_h >= 0.6 && top_plug_rib_w * top_plug_ribs < PI * top_plug_d / 4);
assert(top_seat_floor_t >= 1.6);
assert(top_boss_z0 > top_under_z);
assert(top_screw_engage >= insert_len && top_screw_thread >= min_thread);
assert(top_screw_tip_z - insert_tip_gap >= top_insert_bot_z - 1e-9);
assert(top_insert_hole_depth >= insert_len + 1);
assert(btn_hook_ov >= 0.6 && btn_hook_ov <= 0.8);
assert(btn_tab_flex >= btn_hook_ov + 0.1 - 1e-9);
assert(btn_tab_strain <= 0.013);
assert(btn_tab_t >= 0.8 && btn_tab_t <= btn_wall);
assert(top_plate_t - btn_tab_relief >= 1.0);
assert(btn_relief_y0 - button_hole_d / 2 >= 0.8 - 1e-9);
assert(btn_hook_z0 > top_z0);
assert(btn_bore_d >= btn_flange_d + 0.4);
assert(btn_bore_d / 2 < btn_in - btn_root_in);
assert(btn_tip_z - btn_tip_gap > btn_ceil_z);
assert(btn_ceil_z < top_under_z);
assert(btn_w / 2 - btn_hook_ov >= btn_flange_d / 2 + 0.15 - 1e-9);
assert(btn_tab_w / 2 + btn_tab_slit < btn_in - 1);
assert(norm([button_r + btn_relief_x, btn_relief_y1]) < top_outer_r(btn_root_z) - 1.0);
assert(norm([button_r + btn_tab_w / 2, btn_out + btn_tab_flex]) < top_skirt_in_r);
assert(button_r + button_hole_d / 2 + btn_rim_min < top_face_r);
assert(ring_arc_h <= ring_h && ring_h - ring_arc_h <= 0.15);
assert(ring_above_t >= 0.55 && ring_below_t >= 0.55);
assert(abs(ring_front_wall - ring_front_t) < 1e-9);
assert(ring_pocket_r > fil_d / 2 && ring_neck_w < fil_d);
assert(ring_slit_out >= ring_pocket_r * sqrt(2) / 2);
assert(inner_r < filament_r - ring_slit_in - 0.5);
assert(filament_r + ring_slit_out < case_r - 0.5);
assert(ring_in_r <= top_skirt_in_r - 0.3);
assert(top_screw_r + boss_od / 2 <= ring_in_r - ring_in_clr);
assert(fil_bend_r >= fil_bend_r_min + (chan_d - fil_d) - 1e-9);
assert(fil_ring_len > 0 && fil_circ - fil_ring_len >= btn_pocket_arc);
assert(norm([fil_tail_end[0], fil_tail_end[1]]) + fil_d / 2 <= inner_r - 1);
assert(norm([fil_contact_start[0], fil_contact_start[1]]) + fil_d / 2 <= inner_r + 1e-3);
assert(fil_col_clr >= 0.5);
assert(top_gasket_squeeze / top_gasket_t >= 0.2 && top_gasket_squeeze / top_gasket_t <= 0.3);
assert(top_groove_out_r - top_groove_in_r >= top_gasket_w * top_gasket_t / top_gasket_c - 1e-9);
assert(top_gasket_id / 2 > top_groove_in_r);
assert(top_groove_wall >= 0.5);
assert(top_gasket_fit >= -0.25 && top_gasket_fit <= -0.15);
assert(top_gasket_id / 2 - top_groove_in_r >= 0.15);
assert(tray_screw_r + insert_d / 2 <= case_r - 2.5);
assert(tray_screw_r + boss_od / 2 <= case_r - 0.5);
assert(tray_screw_r - boss_od / 2 > spk_magnet_d / 2 + 2);
assert(body_bot_z > usb_z + usb_plug_h / 2 + 1.2);
assert(mic_z - mic_fill_r > body_bot_z + 1);
assert(case_r - mic_face_r - mic_port_flare >= 1.0 && mic_port_out_d > mic_port_d);
assert(tray_rail_out_x - (dk_row_x + tray_slot_w / 2) >= dk_nest_wall);
assert(tray_pin_floor_z >= 0.6);
assert(tray_rail_top_z < dk_z0 && tray_rail_top_z > dk_hdr_z0 + 1.5);
assert(dk_w / 2 - (dk_row_x + tray_pin_groove_w / 2) >= 0.5);
assert(dks_latch_strain <= 0.016 && dkf_latch_strain <= 0.014 && dkf_latch_strain <= dks_latch_strain);
assert(dkf_stop_clr <= 0.1 && dkf_latch_c >= dkf_stop_clr + 0.25 && dkf_hook_touch >= dkf_stop_clr + 0.05 && dkf_stop_t >= 0.8);
assert(dkf_latch_ov - dk_back_travel >= 0.25 && dk_back_travel <= board_clr);
assert(dkf_latch_fillet >= latch_root_fillet && dkf_latch_t >= 0.65);
assert(tray_latch_root_z >= tray_skin_t - 1e-9);
assert(dkf_latch_x0 > dk_ant_w / 2 + 0.3 && dkf_latch_x0 > dk_mod_w / 2 + 0.3 && dkf_latch_x1 <= dk_w / 2);
assert(dk_y1 - dkf_latch_ov > dk_pin_y1 + dk_pin_sq / 2 + 1);
assert(cap_axis_y - cap_d / 2 > dks_latch_y + dks_latch_w / 2 + 1);
assert(norm([amp_x0 + amp_l + amp_clr, amp_y + amp_w / 2 + amp_clr]) <= part_r - 0.5);
assert(dk_w / 2 - dks_latch_ov > dk_com_x + dk_usb_w / 2 + 1 && dk_w / 2 - dks_latch_ov > dk_row_x + dk_pin_sq / 2 + 0.5);
assert(tray_amp_pad_lx1 > 0);
assert(amp_latch_strain <= 0.025);
assert(amp_lip_z + 0.5 <= tray_rail_top_z);
assert(amp_lip_ov > amp_latch_c + 0.3 && amp_latch_ov > amp_lip_gap + 0.25);
assert(max(amp_latch_lx + amp_latch_w / 2 + latch_relief_clr, amp_lip_lx + amp_lip_w / 2) < amp_w / 2 - amp_hdr_in - amp_hdr_w - 0.5);
assert(min(amp_latch_lx - amp_latch_w / 2 - latch_relief_clr, amp_lip_lx - amp_lip_w / 2) > -amp_w / 2 + 1);
assert(tray_amp_bar_lx0 < amp_w / 2 - 0.5);
assert(amp_z0 - amp_hdr_tail - tray_floor_t >= 0.3);
assert(tray_screw_thread >= min_thread && tray_screw_engage >= insert_len);
assert(tray_screw_tip_z + insert_tip_gap <= tray_insert_top_z + 1e-9);
assert(tray_seat_z + csk_h <= body_bot_z - 1.5);
assert(tray_access_d >= csk_d + 1.0);
assert(tray_screw_r + tray_access_d / 2 <= case_r - 1.2);
assert(tray_screw_r + screw_thru_d / 2 < tray_groove_in_r - 0.5);
assert(tray_gasket_squeeze / tray_gasket_t >= 0.2 && tray_gasket_squeeze / tray_gasket_t <= 0.3);
assert(tray_groove_out_r - tray_groove_in_r >= tray_gasket_w * tray_gasket_t / tray_gasket_c - 1e-9);
assert(tray_gasket_id / 2 > tray_groove_in_r);
assert(tray_gasket_fit >= -0.25 && tray_gasket_fit <= -0.15);
assert(tray_gasket_id / 2 - tray_groove_in_r >= 0.15);
assert(tray_groove_wall >= 0.5);
assert(tray_com_web >= 0.5);
assert(com_bush_fit >= 0.1 && com_bush_fit <= 0.2);
assert(tray_com_w - com_hole_w >= 2 * com_bush_min_wall && tray_com_h - com_hole_h >= 2 * com_bush_min_wall);
assert(com_bush_y1 < com_slot_far_y && com_bush_y1 < usb_plug_face_y);
assert(foot_out >= 1.5 && foot_out <= 2.5);
assert(foot_niche_skin >= 0.8 && foot_niche_h > 1);
assert(foot_niche_clr >= 0.2 && foot_niche_clr <= 0.5);
assert(foot_pad_grow >= 2.0);
assert(foot_corner_r * 2 < min(foot_w, foot_l));
assert(len(foot_xy) == 4 && len(foot_rot) == 4);
