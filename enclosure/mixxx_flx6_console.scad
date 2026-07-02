// =============================================================================
//  CONSOLE MIXXX — coque inclinee pour ecran 10.1" + Raspberry Pi 4
//  Style standalone (XDJ / Denon SC Live) posee sur un Pioneer DDJ-FLX6
// =============================================================================
//  Modele PARAMETRIQUE : change les valeurs dans la section PARAMETRES,
//  puis previsualise (F5) / rends (F6) / exporte le STL (F7) dans OpenSCAD.
//
//  Repere natif du modele (avant orientation d'affichage) :
//     X = profondeur (avant -> arriere)
//     Y = hauteur    (pose sur le FLX6 a Y=0)
//     Z = largeur    (gauche Z=0  ->  droite Z=largeur)
//  La fonction orient() redresse tout pour l'affichage (Z = haut).
//
//  >>> A MESURER SUR TON MATERIEL puis a corriger ici (marques "TODO MESURE")
//      - vis_w / vis_incline : la zone VISIBLE du verre (pas le panneau entier)
//      - position exacte des ports sur la tranche gauche de l'ecran
//      - largeur de la zone plate du FLX6 ou la coque se pose (doit etre >= housing_w)
// =============================================================================

$fn = 48;          // lissage cercles (48 = preview rapide ; 96 pour export final)
eps = 0.1;         // petit jeu pour des decoupes propres

part = "all";      // "all" = apercu assemble | "shell" = coque seule | "back" = capot arriere

// ----------------------------- PARAMETRES ------------------------------------

// --- Ecran : LAFVIN 10.1" 1024x600 HDMI IPS tactile (panneau complet mesure) -
screen_w   = 235;  // largeur panneau (carte comprise)   [mm]
screen_h   = 145;  // hauteur panneau (dimension le long de l'inclinaison) [mm]
screen_t   = 16;   // epaisseur panneau + carte controleur [mm]

vis_w       = 222; // TODO MESURE : largeur zone VISIBLE du verre  [mm]
vis_incline = 125; // TODO MESURE : hauteur zone VISIBLE du verre   [mm]

// --- Carcasse ----------------------------------------------------------------
wall   = 3;        // epaisseur parois (PETG conseille : resiste a la chaleur du Pi)
bezel  = 6;        // marge de cadre autour du panneau (le "bezel" visible)
tilt   = 22;       // inclinaison ecran depuis la verticale [deg] (CDJ ~15-25)
front_rail = 14;   // hauteur du rebord avant sous l'ecran [mm]
base_depth = 130;  // profondeur de l'embase posee sur le FLX6 [mm]

// --- Raspberry Pi 4 (loge derriere l'ecran, a plat sur l'embase) -------------
pi_l        = 85;  // longueur carte (le long de la profondeur X) [mm]
pi_w        = 56;  // largeur carte  (le long de Z)               [mm]
pi_holes_l  = 58;  // entraxe trous sur la longueur               [mm]
pi_holes_w  = 49;  // entraxe trous sur la largeur                [mm]
pi_hole_d   = 2.7; // diametre trous (vis M2.5)                   [mm]
pi_standoff = 6;   // hauteur des plots de fixation               [mm]
pi_margin   = 3.5; // retrait des trous depuis le bord de la carte
pi_pos_x    = 62;  // position avant de la carte le long de X     [mm]
pi_pos_z    = 0;   // decalage de la carte en Z (0 = centree)     [mm]

// --- Ventilation -------------------------------------------------------------
fan_size  = 40;    // ventilateur 40 mm (pilotable par le port FAN de l'ecran)
fan_screw = 32;    // entraxe vis du ventilateur 40 mm
vent_n    = 7;     // nombre de fentes d'aeration sur le capot arriere
vent_slot = 3;     // largeur d'une fente [mm]

// --- Capot arriere / visserie ------------------------------------------------
plate_t   = 3;     // epaisseur du capot arriere [mm]
boss_d    = 8;     // diametre des plots de vissage du capot
boss_hole = 2.7;   // avant-trou vis M3 dans les plots
boss_len  = 16;    // longueur des plots (vers l'interieur)

// --- Sortie cables (tranche gauche, la ou sont les ports de l'ecran) ---------
cable_w   = 45;    // largeur de l'ouverture cables [mm]
cable_h   = 22;    // hauteur de l'ouverture cables [mm]
cable_x   = 30;    // position en profondeur de l'ouverture [mm]
cable_y   = 22;    // hauteur de l'ouverture depuis l'embase [mm]

// --------------------------- VALEURS DERIVEES --------------------------------
housing_w = screen_w + 2*bezel + 2*wall;     // largeur hors-tout (Z)
panel_len = screen_h + 2*bezel;              // longueur de la face inclinee
scr_lo    = [0, front_rail];                 // bas de l'ecran (avant) [X,Y]
scr_dir   = [sin(tilt), cos(tilt)];          // direction du plan ecran
scr_hi    = [scr_lo[0] + panel_len*scr_dir[0],
             scr_lo[1] + panel_len*scr_dir[1]];   // haut de l'ecran (arriere)
top_h     = scr_hi[1];                        // hauteur hors-tout (Y)

// Profil lateral de la coque (vue de cote, plan X-Y) :
profile = [ [0,0], scr_lo, scr_hi, [base_depth, top_h], [base_depth, 0] ];

echo(str(">> Hors-tout L x P x H = ", housing_w, " x ", base_depth, " x ", top_h, " mm"));

// ============================== MODULES =======================================

// Redresse le modele pour l'affichage : Y(hauteur) -> Z(haut)
module orient() { translate([0,0,top_h]) rotate([-90,0,0]) children(); }

// Decoupe de l'ecran : fenetre visible (traversante) + logement du panneau
module screen_cut() {
    translate([scr_hi[0]/2 + scr_lo[0]/2, scr_hi[1]/2 + scr_lo[1]/2, housing_w/2])
        rotate([0, 0, 90 - tilt]) {          // aligne l'axe X local sur le plan ecran
            // fenetre visible (traverse la paroi avant)
            cube([vis_incline, 4*wall, vis_w], center=true);
            // logement du panneau : DERRIERE le cadre seulement (le bezel
            // reste plein et retient le panneau par l'avant)
            translate([0, -(wall + (screen_t + 2)/2), 0])
                cube([panel_len - 2*bezel + 0.6, screen_t + 2, screen_w + 0.6], center=true);
        }
}

// Plots de fixation du Pi 4 (4 plots avant-troues sur l'embase)
module pi_mount() {
    cx = pi_pos_z + (housing_w - pi_w)/2;     // origine Z de la carte (centree + decalage)
    for (dx = [pi_margin, pi_margin + pi_holes_l])
        for (dz = [pi_margin, pi_margin + pi_holes_w])
            translate([pi_pos_x + dx, wall, cx + dz])
                difference() {
                    cylinder(h = pi_standoff, d = 6);
                    translate([0,-eps,0]) cylinder(h = pi_standoff + 1, d = pi_hole_d);
                }
}

// Sortie cables sur la tranche gauche (Z = 0)
module cable_port() {
    translate([cable_x, cable_y, -eps])
        cube([cable_w, cable_h, wall + 2*eps]);
}

// Plots de vissage du capot arriere (4 coins de l'ouverture)
module back_bosses() {
    inset = wall + boss_d/2 + 2;
    for (y = [inset, top_h - inset])
        for (z = [inset, housing_w - inset])
            translate([base_depth - boss_len, y, z])
                rotate([0,90,0])
                    difference() {
                        cylinder(h = boss_len, d = boss_d);
                        translate([0,0,-eps]) cylinder(h = boss_len + 2*eps, d = boss_hole);
                    }
}

// ----------------------------- COQUE PRINCIPALE ------------------------------
module shell() {
    difference() {
        union() {
            // corps creux : profil plein moins profil retreci (parois sur tous cotes)
            difference() {
                linear_extrude(housing_w) polygon(profile);
                translate([0, 0, wall])
                    linear_extrude(housing_w - 2*wall)
                        offset(delta = -wall) polygon(profile);
                // ouvre la face arriere (le capot la refermera)
                translate([base_depth - wall - eps, wall, wall])
                    cube([wall + 2*eps, top_h - 2*wall, housing_w - 2*wall]);
            }
            pi_mount();
            back_bosses();
        }
        screen_cut();
        cable_port();
    }
}

// ----------------------------- CAPOT ARRIERE ---------------------------------
module back_plate() {
    inset = wall + boss_d/2 + 2;
    fan_cz = pi_pos_z + housing_w/2;          // ventilo centre en largeur
    fan_cy = top_h/2;                          // ... et en hauteur
    difference() {
        // plaque pleine sur la face arriere
        translate([base_depth, 0, 0]) cube([plate_t, top_h, housing_w]);
        // trous de vis (alignes sur les plots)
        for (y = [inset, top_h - inset])
            for (z = [inset, housing_w - inset])
                translate([base_depth - eps, y, z])
                    rotate([0,90,0]) cylinder(h = plate_t + 2*eps, d = boss_hole + 0.6);
        // ouverture + trous du ventilateur 40 mm
        translate([base_depth - eps, fan_cy, fan_cz])
            rotate([0,90,0]) cylinder(h = plate_t + 2*eps, d = fan_size - 4);
        for (dy = [-fan_screw/2, fan_screw/2])
            for (dz = [-fan_screw/2, fan_screw/2])
                translate([base_depth - eps, fan_cy + dy, fan_cz + dz])
                    rotate([0,90,0]) cylinder(h = plate_t + 2*eps, d = 3.2);
        // fentes d'aeration verticales traversant la plaque (sous le ventilo)
        for (i = [0 : vent_n - 1])
            translate([base_depth - eps,
                       wall + 10,
                       housing_w/2 - (vent_n-1)*6/2 + i*6])
                cube([plate_t + 2*eps, 55, vent_slot]);
    }
}

// ================================ ASSEMBLAGE =================================
orient() {
    if (part == "all" || part == "shell") shell();
    if (part == "all" || part == "back")  color("DimGray") back_plate();
}
