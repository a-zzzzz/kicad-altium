/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 1992-2013 Jean-Pierre Charras <jp.charras at wanadoo.fr>.
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

/*
 * keyword used in drawing sheet are listed in drawing_sheet.keywords file
 */

/*
 *  Items use coordinates.
 *  A coordinate is defined relative to a page corner
 *  A coordinate is the X pos, the Y pos, and the corner which is the coordinate origin
 *  the default is the bottom right corner
 *  example: (start 10 0 ltcorner) or (start 20 10)
 *  The direction depends on the corner: a positive coordinate define a point
 *  from the corner origin, to the opposite corner.
 *
 *  Items are defined by a name, coordinates in mm and some attributes,
 *  and can be repeated.
 *  for instance (repeat 2) (incrx 2) (incry 2) repeat the item 2 times,
 *  and coordinates are incremented by 2 on X direction, and 2 on Y direction
 *  Comments are allowed. they are inside (), and start by the keyword comment
 *  example:
 *  (comment rect around the title block)
 *
 *  Lines and rect are defined by 2 coordinates start and end, and attributes.
 *  Attributes are linewidth and repeat parameters.
 *  example:
 *  (line (start 50 2 ltcorner) (end 50 0 ltcorner) (repeat 30) (incrx 50) )
 *  (rect (comment rect around the title block) (linewidth 0.15) (start 110 34) (end 2 2) )
 *
 *  Texts are defined by the text (between quotes), the position, and attributes
 *  example
 *  "(tbtext \"1\" (pos 25 1 lbcorner) (font (size 1.3 1.3)) (repeat 100) (incrx 50) )"
 *  the text can be rotated by (rotation <value>) with value = rot angle in degrees
 *  (font (size 1.3 1.3) bold italic) defines a specific size,
 *    with bold and italic options
 *  (justify <justif keyword>) controls the text justification (the default is left)
 *  justif keyword is center, left, right, top and bottom
 *  (justify center top) is a text centered on X axis and top aligned on vertical axis
 *  The text size can be constrained:
 *  (maxlen <value>) and (maxheight <value>) force the actual text x and/or y size to be
 *  reduced to limit the text height to the maxheight value,
 *  and the full text x size to the maxlen value.
 *  If the actual text size is smaller than limits, its size is not modified.
 *
 * Texts can include a format symbol, a la printf.
 * At run time these format symbols will be replaced by their actual value.
 *
 * format symbols are:
 *
 * %% = replaced by %
 * %K = Kicad version
 * %Z = paper format name (A4, USLetter ...)
 * %Y = company name
 * %D = date
 * %R = revision
 * %S = sheet number
 * %N = number of sheets
 * %Cx = comment (x = 0 to 9 to identify the comment)
 * %F = filename
 * %P = sheet path (sheet full name)
 * %T = title
 *
 * example:
 * (tbtext \"Size: %Z\" ...) displays "Size A4" or Size USLetter"
 *
 *  Poly Polygons
 *  Set of filled polygons are supported.
 *
 *  The main purpose is to allow logos, or complex shapes
 *  They support the repeat and rotation options
 *  They are defined by
 *  (polygon (position ..) <rotation>  <linewidth>
 *  the parameter linewidth defines the pen size used to draw/plot
 *  the polygon outlines (default = 0)
 *  example:
 *  (polygon (pos 134 18 rbcorner) (rotate 20)  (linewidth 0.00254)
 *
 *  and a list of corners like
 *  (pts (xy 20.574 8.382) (xy 19.9009 8.382) (xy 19.9009 6.26364) (xy 19.7485 5.98932)
 *  .... )
 *
 *  each sequence like
 *  (pts (xy 20.574 8.382) (xy 19.9009 8.382) (xy 19.9009 6.26364) (xy 19.7485 5.98932)
 *  .... )
 *  defines a polygon.
 *  Each coordinate is relative to the polygon position.
 *  Therefore a "polygon" is in fact a set of polygons, of a poly polygon
 *
 */


// height of the coordinates band 2.0 mm
// coordinates text size 1.3 mm
// default text size 1.5 mm
// default line width 0.15 mm
// frame ref pitch 50 mm

// Default drawing sheet (sizes are in mm).  Title block laid out like Altium's default
// sheet template (sheet / project title; size, item, revision; date, time, sheet of; file;
// organisation column), drawn for this fork.
extern const char defaultDrawingSheet[] =
    "(kicad_wks (version 20210606) (generator pl_editor)\n"
    "(setup (textsize 1.5 1.5)(linewidth 0.15)(textlinewidth 0.15)\n"
    "(left_margin 10)(right_margin 10)(top_margin 10)(bottom_margin 10))\n"
    "(rect (name \"\") (start 0 0 ltcorner) (end 0 0) (repeat 2) (incrx 2) (incry 2))\n"
    "(line (name \"\") (start 50 2 ltcorner) (end 50 0 ltcorner) (repeat 30) (incrx 50))\n"
    "(tbtext \"1\" (name \"\") (pos 25 1 ltcorner) (font (size 1.3 1.3)) (repeat 100) (incrx 50))\n"
    "(line (name \"\") (start 50 2 lbcorner) (end 50 0 lbcorner) (repeat 30) (incrx 50))\n"
    "(tbtext \"1\" (name \"\") (pos 25 1 lbcorner) (font (size 1.3 1.3)) (repeat 100) (incrx 50))\n"
    "(line (name \"\") (start 0 50 ltcorner) (end 2 50 ltcorner) (repeat 30) (incry 50))\n"
    "(tbtext \"A\" (name \"\") (pos 1 25 ltcorner) (font (size 1.3 1.3)) (justify center) (repeat 100) (incry 50))\n"
    "(line (name \"\") (start 0 50 rtcorner) (end 2 50 rtcorner) (repeat 30) (incry 50))\n"
    "(tbtext \"A\" (name \"\") (pos 1 25 rtcorner) (font (size 1.3 1.3)) (justify center) (repeat 100) (incry 50))\n"
    "(rect (name \"\") (start 154 28) (end 2 2) (comment \"rect around the title block\"))\n"
    "(line (name \"\") (start 154 21.2) (end 71 21.2))\n"
    "(line (name \"\") (start 154 14.6) (end 71 14.6))\n"
    "(line (name \"\") (start 154 8.6) (end 71 8.6))\n"
    "(line (name \"\") (start 154 5.2) (end 71 5.2))\n"
    "(line (name \"\") (start 131 14.6) (end 131 8.6))\n"
    "(line (name \"\") (start 100.5 14.6) (end 100.5 5.2))\n"
    "(line (name \"\") (start 71 28) (end 71 2))\n"
    "(tbtext \"Sheet Title\" (name \"\") (pos 152.5 24.2) (font (face \"Times New Roman\") (size 1.8 1.8)))\n"
    "(tbtext \"${TITLE}\" (name \"\") (pos 135 24.6) (font (face \"Times New Roman\") (size 3.2 3.2) bold italic (color 0 0 255 1)) (maxlen 62) (comment \"Sheet title\"))\n"
    "(tbtext \"Project Title\" (name \"\") (pos 152.5 17.5) (font (face \"Times New Roman\") (size 1.8 1.8)))\n"
    "(tbtext \"${PROJECTNAME}\" (name \"\") (pos 135 17.5) (font (face \"Times New Roman\") (size 2 2) italic (color 0 0 255 1)) (maxlen 62))\n"
    "(tbtext \"Size:\" (name \"\") (pos 152.5 11.2) (font (face \"Times New Roman\") (size 1.8 1.8)))\n"
    "(tbtext \"${PAPER}\" (name \"\") (pos 145 11.2) (font (face \"Times New Roman\") (size 2 2) (color 0 0 255 1)) (comment \"Paper format name\"))\n"
    "(tbtext \"Item:\" (name \"\") (pos 130 11.2) (font (face \"Times New Roman\") (size 1.8 1.8)))\n"
    "(tbtext \"${COMMENT1}\" (name \"\") (pos 122.5 11.2) (font (face \"Times New Roman\") (size 1.8 1.8) (color 0 0 255 1)) (maxlen 21) (comment \"Item / document number\"))\n"
    "(tbtext \"Revision:\" (name \"\") (pos 99.5 11.2) (font (face \"Times New Roman\") (size 1.8 1.8)))\n"
    "(tbtext \"${REVISION}\" (name \"\") (pos 87.5 11.2) (font (face \"Times New Roman\") (size 1.8 1.8) (color 0 0 255 1)) (maxlen 15))\n"
    "(tbtext \"Date:\" (name \"\") (pos 152.5 6.6) (font (face \"Times New Roman\") (size 1.6 1.6)))\n"
    "(tbtext \"${ISSUE_DATE}\" (name \"\") (pos 145 6.6) (font (face \"Times New Roman\") (size 1.6 1.6) (color 0 0 255 1)) (maxlen 13))\n"
    "(tbtext \"Time:\" (name \"\") (pos 130 6.6) (font (face \"Times New Roman\") (size 1.6 1.6)))\n"
    "(tbtext \"${CURRENT_TIME_LOCALE}\" (name \"\") (pos 122.5 6.6) (font (face \"Times New Roman\") (size 1.6 1.6) (color 0 0 255 1)) (maxlen 21))\n"
    "(tbtext \"Sheet\" (name \"\") (pos 99.5 6.6) (font (face \"Times New Roman\") (size 1.6 1.6)))\n"
    "(tbtext \"${#}\" (name \"\") (pos 91 6.6) (font (face \"Times New Roman\") (size 1.6 1.6) (color 0 0 255 1)) (comment \"Sheet number\"))\n"
    "(tbtext \"of\" (name \"\") (pos 85 6.6) (font (face \"Times New Roman\") (size 1.6 1.6)))\n"
    "(tbtext \"${##}\" (name \"\") (pos 80.5 6.6) (font (face \"Times New Roman\") (size 1.6 1.6) (color 0 0 255 1)))\n"
    "(tbtext \"File:\" (name \"\") (pos 152.5 3.3) (font (face \"Times New Roman\") (size 1.6 1.6)))\n"
    "(tbtext \"${FILENAME}\" (name \"\") (pos 145 3.3) (font (face \"Times New Roman\") (size 1.6 1.6) (color 0 0 255 1)) (maxlen 72))\n"
    "(tbtext \"${COMPANY}\" (name \"\") (pos 69.5 25.3) (font (face \"Times New Roman\") (size 1.8 1.8) italic (color 0 0 255 1)) (maxlen 66) (comment \"Organisation\"))\n"
    "(tbtext \"${COMMENT2}\" (name \"\") (pos 69.5 22.1) (font (face \"Times New Roman\") (size 1.8 1.8) italic (color 0 0 255 1)) (maxlen 66) (comment \"Address 1\"))\n"
    "(tbtext \"${COMMENT3}\" (name \"\") (pos 69.5 18.9) (font (face \"Times New Roman\") (size 1.8 1.8) italic (color 0 0 255 1)) (maxlen 66) (comment \"Address 2\"))\n"
    "(tbtext \"${COMMENT4}\" (name \"\") (pos 69.5 15.7) (font (face \"Times New Roman\") (size 1.8 1.8) italic (color 0 0 255 1)) (maxlen 66) (comment \"Address 3\"))\n"
    "(tbtext \"${COMMENT5}\" (name \"\") (pos 69.5 12.5) (font (face \"Times New Roman\") (size 1.8 1.8) italic (color 0 0 255 1)) (maxlen 66) (comment \"Address 4\"))\n"
    ")\n";

extern const char emptyDrawingSheet[] =
    "(kicad_wks (version 20210606) (generator pl_editor)\n"
    "(setup (textsize 1.5 1.5)(linewidth 0.15)(textlinewidth 0.15)\n"
    "(left_margin 10)(right_margin 10)(top_margin 10)(bottom_margin 10))\n"
    "(line (name \"segm1:Line\") (start 0 0) (end 0 0))\n"
    ")\n";
