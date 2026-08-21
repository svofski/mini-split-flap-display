// Final quality
render = false;

// Headlights are separate objects
separate_headlights = true;

// mode=0: generate everything
$fs = 0.4;//(render || mode >= 100) ? 0.5 : 1;

layer=0.2;

flap_width=22;
flap_height=16;
flap_thickness=0.8;
flap_gap=0.2;
font_size=22;
baseline=-15;
ear_len=2;
ear_offset=1.5; // ideal
barberpole_char="-";

use <Aston.ttf>;

eps=0.01;

chars="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789- ";

letter_thickness = layer * 2;

all();

//glyph("A");
//barberpole();

module all()
{
    nline = floor(sqrt(len(chars)));
    xstep = (flap_width + 2*ear_len + 1);
    ystep = (flap_height + 2);
    for (i = [0:len(chars)-1]) {
        ch1 = chars[i];
        ch2 = chars[(i + 1) % len(chars)];
        translate([xstep * (i % nline), ystep * floor(i / nline), 0])
            colorflap(ch1, ch2);
    }
}

module colorflap(char1, char2) {
    color("black") oneflap(char1,char2, "flap");
    color("white") oneflap(char1,char2, "glyph");
}


module crop()
{
    intersection() {
        color("black") translate([0, -flap_height/2 - flap_gap, 0])
            cube([flap_width + ear_len*2, flap_height + eps*2, flap_thickness + eps*2], center = true);
        children(0);
    }
}

module oneflap(char1, char2, mode="flap")
{
    if (mode == "flap") {
        difference() {
            flap([flap_width,flap_height,flap_thickness], fillet_r=3);
            color("blue")
                crop()
                    translate([0,0,flap_thickness/2 - letter_thickness + eps])
                        glyph(char2);
            color("green") 
                crop()
                    translate([0,0,-flap_thickness/2 + letter_thickness - letter_thickness - eps])
                        scale([1,-1,1])
                            glyph(char1);
        }
    }
    else {
            color("blue")
                crop()
                    translate([0,0,flap_thickness/2 - letter_thickness + eps])
                        glyph(char2);
            color("green") 
                crop()
                    translate([0,0,-flap_thickness/2 + letter_thickness - letter_thickness - eps])
                        scale([1,-1,1])
                            glyph(char1);
    }
}

module glyph(letter)
{
    if (letter == barberpole_char) {
        barberpole();
    }
    else {
        linear_extrude(height=letter_thickness) 
            translate([0,baseline,0])
                text(letter, font="Aston:style=Regular", halign="center", valign="baseline", size=font_size);    
    }
}


module barberpole(pitch = font_size / 3)
{
    intersection() {
        union() {
            flap([flap_width,flap_height,flap_thickness], fillet_r=3);
            scale([1,-1,1]) flap([flap_width,flap_height,flap_thickness], fillet_r=3);
        }

        rotate([0,0,-45]) 
        translate([-font_size, -font_size, 0])
        for(i = [0:pitch:font_size * 2]) {
            translate([i, 0, 0]) 
                cube([pitch/2, font_size * 2, letter_thickness]);
        }
    }
}

module flap(sides, gap=flap_gap, ear_len=ear_len, ear_width=1.6, ear_offset=ear_offset, fillet_r = 3)
{
    hull() {
        translate([-sides.x/2 + fillet_r, -sides.y + fillet_r, 0])
            cylinder(h = sides.z, r = fillet_r, center = true);
        translate([+sides.x/2 - fillet_r, -sides.y + fillet_r, 0])
            cylinder(h = sides.z, r = fillet_r, center = true);
        
        translate([0,-gap*2,0])
        cube([sides.x, gap*2, sides.z], center = true);
    }
    
    
    translate([0, -(gap + ear_offset), 0])
    cube([sides.x + ear_len * 2, ear_width, sides.z], center = true);
    
}
