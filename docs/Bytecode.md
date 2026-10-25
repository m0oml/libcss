CSS style declaration bytecode
==============================

Format
------

```
<opcode+flags+value> [<parameters>]

<opcode+flags+value> is 32 bits wide:
	bits 18-31: value
	bits 10-17 : flags
	bits 0-9  : opcode

The 8 bits of flag data are defined as follows:
	bits 4-7: Must Be Zero (MBZ)
	bits 1-3: Generic value
		000 => no generic value
		001 => inherit
		010 => initial
		011 => revert
		100 => unset
		101 => uses var()
	bit 0   : value is important
```

The 14 bits of value are opcode-specific.

A declaration whose value uses `var()` cannot be parsed until the custom
properties are known. It has the generic value 101 and a value of zero for
each property it sets (all of a shorthand's properties), followed by two
string table indices: the text of the value and the name of the property
as declared, which may be a shorthand. The value is parsed during
selection, after the `var()` references have been substituted.

Parameters are opcode-specific. Each parameter must begin on a 4 byte boundary.

Datatype storage
----------------

All numeric values are stored in a 32bit wide field. This field contains
a fixed point value with 22 bits assigned to the integer part and 10 bits
assigned to the fractional part.

Strings are stored as a 32bit index into a table of interned string pointers.
The table is found in the stylesheet object.

CSS dimensions are stored as two 32bit values: `<length, units>`.
Length is a 32bit numeric value (as described above) and unit is as follows:

```
	bit 8 set => length unit
		bits 9-31: MBZ
		bits 0-7 :
			00000000 => px
			00000001 => ex
			00000010 => em
			00000011 => in
			00000100 => cm
			00000101 => mm
			00000110 => pt
			00000111 => pc
			00001000 => ch
			00001001 => rem
			00001010 => lh
			00001011 => vh
			00001100 => vw
			00001101 => vi
			00001110 => vb
			00001111 => vmin
			00010000 => vmax
			00010001 => q

	bit 9 set => percentage unit
		bits 9-31: MBZ
		bits 0-7 : MBZ

	bit 10 set => angle unit
		bits 10-31: MBZ
		bit 8     : MBZ
		bits 0-7  :
			00000000 => deg
			00000001 => grad
			00000010 => rad
			00000011 => turn

	bit 11 set => time unit
		bits 11-31: MBZ
		bits 8-9  : MBZ
		bits 0-7  :
			00000000 => ms
			00000001 => s

	bit 12 set => frequency unit
		bits 12-31: MBZ
		bits 8-10 : MBZ
		bits 0-7  :
			00000000 => Hz
			00000001 => kHz

	bit 13 set => resolution unit
		bits 13-31: MBZ
		bits 8-11 : MBZ
		bits 0-7  :
			00000000 => dpi
			00000001 => dpcm
			00000010 => dppx
```

CSS colours are stored as one 32bit value. See "Colour" for their format.

Shorthand properties
--------------------

The CSS shorthand properties are handled by decomposing them to their
component parts, and then creating bytecode for these.

For example, "background: red none no-repeat scroll left top !important;" would
be decomposed to:

```css
background-color: red !important;
background-image: none !important;
background-repeat: no-repeat !important;
background-attachment: scroll !important;
background-position: left top !important;
```

and bytecode generated for each of these properties.

The full list of CSS 2.1 shorthand properties is:

```
background
border-color
border-style
border-{top,right,bottom,left}
border-width
border
cue
font
list-style
margin
outline
padding
pause
```

Opcodes
-------

```
00 - azimuth
	<value> (14bits) :
		bits 8-13: MBZ
		bit 7 set => angle follows.
			bits 0-6: MBZ
		bit 6 set => relative movement:
			bit 7   : MBZ
			bits 1-5: MBZ
			bit 0   : set => rightwards, clear => leftwards
		bits 6 & 7 clear => keyword position:
			bit 5   : set => behind, clear => in front
			bits 0-4:
				00000  => left-side
				00001  => far-left
				00010  => left
				00011  => center-left
				00100  => center
				00101  => center-right
				00110  => right
				00111  => far-right
				01000  => right-side
				other => Reserved for future expansion.

01 - background-attachment
	<value> (14bits) :
		0     => fixed
		1     => scroll
		other => Reserved for future expansion.

02 - background-color
	<value> (14bits) :
		bit 8-13: MBZ
		bit 7 set => colour follows.
			bits 0-6: MBZ
		bit 7 clear => keyword colour:
			bits 0-6: 0000000 => transparent,
				  0000001 => currentColor,
				  other   => rffe.

03 - background-image
	<value> (14bits) :
		bits 8-13: MBZ
		bit 7 set => uri string follows.
			bits 0-6: MBZ
		bit 7 clear => keyword:
			bits 1-6: MBZ
			bit 0   : clear => none, set => rffe.

04 - background-position
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7: set => percentage or length unit follows
					bits 4-6: MBZ
			       clear => keywords:
					bits 4-6:
						000   => center
						001   => right
						010   => left
						other => rffe
			bit 3: set => percentage or length unit follows
					bits 0-2: MBZ
			       clear => keywords:
					bits 0-2:
						000   => center
						001   => bottom
						010   => top
						other => rffe

05 - background-repeat
	<value> (14bits) :
		0     => no-repeat
		1     => repeat-x
		2     => repeat-y
		3     => repeat
		other => Reserved for future expansion.

06 - border-collapse
	<value> (14bits) :
		0     => separate
		1     => collapse
		other => Reserved for future expansion.

07 - border-spacing
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7: set => two lengths follow
					bits 0-6: MBZ
			       clear => Reserved for future expansion.
			       		bits 0-6: MBZ

08 - border-top-color
09 - border-right-color
0a - border-bottom-color
0b - border-left-color
	<value> (14bits) :
		bits 8-13: MBZ
		bit 7 set => colour follows.
			bits 0-6: MBZ
		bit 7 clear => keyword colour:
			bits 0-6: 0000000 => transparent,
				  0000001 => currentColor,
				  other   => rffe.

0c - border-top-style
0d - border-right-style
0e - border-bottom-style
0f - border-left-style
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			00000000 => none,
			00000001 => hidden,
			00000010 => dotted,
			00000011 => dashed,
			00000100 => solid,
			00000101 => double,
			00000110 => groove,
			00000111 => ridge,
			00001000 => inset,
			00001001 => outset,
			other    => Reserved for future expansion.

10 - border-top-width
11 - border-right-width
12 - border-bottom-width
13 - border-left-width
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => length follows
				bits 0-6: MBZ
			bit 7 clear => keywords:
				bits 0-6: 0000000 => thin,
					  0000001 => medium,
					  0000010 => thick,
					  other   => rffe.

14 - bottom
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7:
			bit 7 set => length/percentage follows
				bits 0-6: MBZ
			bit 7 clear => keywords:
				bits 0-6: 0000000 => auto,
					  other   => rffe.

15 - caption-side
	<value> (14bits) :
		0     => top
		1     => bottom
		other => Reserved for future expansion.

16 - clear
	<value> (14bits) :
		0     => none
		1     => left
		2     => right
		3     => both
		other => Reserved for future expansion.

17 - clip
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7:
			bit 7 set => shape follows
				bits 0-2: 000   => rect,
						   bit 3 => top auto
						   bit 4 => right auto
						   bit 5 => bottom auto
						   bit 6 => left auto
					  other => rffe.
						   bits 3-6: MBZ.

			bit 7 clear => keywords:
				bits 0-6: 0000000 => auto,
					  other   => rffe.

	If the value is rect(top, right, bottom, left), then bits 3-6 encode
	which of <top>, <right>, <bottom>, <left> is set to auto. The
	subsequent parameter list is then 4 - <auto_side> entries long.
	Each entry is a dimension. Entries are always ordered top, right,
	bottom, left.

	For example,
		clip: rect(10px, auto, auto, 10px)
	would produce the following bytecode:

	<02c00017> <00002800> <00000000> <00002800> <00000000>

18 - color
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7:
			bit 7: set => colour follows.
					bits 0-6: MBZ.
			       clear => keywords:
			       		bits 0-6: 0000000 => transparent,
						  0000001 => currentColor,
						  other   => rffe.

19 - content
	<value> (14bits) :
		bits 8-13: MBZ (except counter & counters, see below)
		bits 0-7 :
			bit 7 set => string follows
				bits 0-6: 0000000 => string,
					  0000001 => uri,
					  0000010 => counter,
						     bits 8-13 : list-style-type
					  0000011 => counters,
						     bits 8-13 : list-style-type
					  0000100 => attr,
					  other   => rffe.
			bit 7 clear => keywords:
				bits 0-6: 0000000 => normal,
					  0000001 => none,
					  0000010 => open-quote,
					  0000011 => close-quote,
					  0000100 => no-open-quote,
					  0000101 => no-close-quote,
					  other   => rffe.

	If the value is not "normal", "none", or a generic value ("inherit",
	"initial", "unset", or "revert"), then there is a parameter list.
	Each item is preceded by a <value> word which declares the type of
	the next item. The list is terminated by a word with all bits set
	to zero (the encoding for "normal").

	For example,
		content: open-quote url('http://example.com/')
			 " : " attr(name) " " counter(x) "." counters(y, ".")
			 close-quote;"
	would result in the following bytecode:

	<00080019>
	<00000081> <ptr to "http://example.com">
	<00000080> <ptr to " : ">
	<00000084> <ptr to "name">
	<00000080> <ptr to " ">
	<00000382> <ptr to "x">
	<00000080> <ptr to ".">
	<00000383> <ptr to "y"> <ptr to ".">
	<00000003> <00000000>

1a - counter-increment
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => <string, integer> pair follows
				bits 0-6: 0000000 => <string, integer> pair
					  other   => rffe.
			bit 7 clear => keywords:
				bits 0-6: 0000000 => none,
					  other   => rffe.

	If the value is not "none", or a generic value ("inherit",
	"initial", "unset", or "revert"), then there is a parameter
	list. Each item is preceded by a <value> word which declares the type
	of the next item. The list is terminated by a word with all bits set to
	zero (the encoding for "none").

1b - counter-reset
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => <string, integer> pair follows
				bits 0-6: 0000000 => <string, integer> pair,
					  other   => rffe.
			bit 7 clear => keywords:
				bits 0-6: 0000000 => none,
					  other   => rffe.

	If the value is not "none", or a generic value ("inherit",
	"initial", "unset", or "revert"), then there is a parameter
	list. Each item is preceded by a <value> word which declares the type
	of the next item. The list is terminated by a word with all bits set to
	zero (the encoding for "none").

1c - cue-after
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => uri follows
				bits 0-6: MBZ
			bit 7 clear => keywords:
				bits 0-6: 0000000 => none,
					  other   => rffe.

1d - cue-before
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => uri follows
				bits 0-6: MBZ
			bit 7 clear => keywords:
				bits 0-6: 0000000 => none,
					  other   => rffe.

1e - cursor
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => uri follows
				bits 0-6: MBZ
			bit 7 clear => keywords:
				bits 0-6: 0000000 => auto,
					  0000001 => crosshair,
					  0000010 => default,
					  0000011 => pointer,
					  0000100 => move,
					  0000101 => e-resize,
					  0000110 => ne-resize,
					  0000111 => nw-resize,
					  0001000 => n-resize,
					  0001001 => se-resize,
					  0001010 => sw-resize,
					  0001011 => s-resize,
					  0001100 => w-resize,
					  0001101 => text,
					  0001110 => wait,
					  0001111 => help,
					  0010000 => progress,
					  other   => rffe.

	If the value indicates that a uri is present, then there is a
	parameter list. Each item is preceded by a <value> word which declares
	the type of the next item. The list is terminated by a word with
	bit 7 clear.

1f - direction
	<value> (14bits) :
		0     => ltr,
		1     => rtl,
		other => Reserved for future expansion.

20 - display
	<value> (14bits) :
		0     => inline,
		1     => block,
		2     => list-item,
		3     => run-in,
		4     => inline-block,
		5     => table,
		6     => inline-table,
		7     => table-row-group,
		8     => table-header-group,
		9     => table-footer-group,
		a     => table-row,
		b     => table-column-group,
		c     => table-column,
		d     => table-cell,
		e     => table-caption,
		f     => none,
		10    => flex,
		11    => inline-flex,
		12    => grid,
		13    => inline-grid
		other => Reserved for future expansion.

21 - elevation
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => angle follows
				bits 0-6: MBZ
			bit 7 clear => keywords:
				bits 0-6: 0000000 => below,
					  0000001 => level,
					  0000010 => above,
					  0000011 => higher,
					  0000100 => lower,
					  other   => rffe.

22 - empty-cells
	<value> (14bits) :
		0     => show,
		1     => hide,
		other => Reserved for future expansion.

23 - float
	<value> (14bits) :
		0     => left,
		1     => right,
		2     => none,
		other => Reserved for future expansion.

24 - font-family
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => data follows
				bits 1-6: MBZ
				bit  0  : clear => string,
					  set => ident list (as string)
			bit 7 clear => keywords:
				bits 0-6: 0000000 => Reserved.
					  0000001 => serif,
					  0000010 => sans-serif,
					  0000011 => cursive,
					  0000100 => fantasy,
					  0000101 => monospace,
					  other   => rffe.

	In all cases, there is a parameter list. Each item is preceded by a
	<value> word which declares the type of the next item. The list is
	terminated by a word with all bits clear.

25 - font-size
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7:
			bit 7 set => dimension follows
				bits 0-6: MBZ
			bit 7 clear => keywords:
				bits 0-6: 0000000 => xx-small,
					  0000001 => x-small,
					  0000010 => small,
					  0000011 => medium,
					  0000100 => large,
					  0000101 => x-large,
					  0000110 => xx-large,
					  0000111 => larger,
					  0001000 => smaller,
					  other   => rffe.

26 - font-style
	<value> (14bits) :
		0     => normal,
		1     => italic,
		2     => oblique,
		other => Reserved for future expansion.

27 - font-variant
	<value> (14bits) :
		0     => normal,
		1     => small-caps,
		other => Reserved for future expansion.

28 - font-weight
	<value> (14bits) :
		0     => normal,
		1     => bold,
		2     => bolder,
		3     => lighter,
		4     => 100,
		5     => 200,
		6     => 300,
		7     => 400,
		8     => 500,
		9     => 600,
		a     => 700,
		b     => 800,
		c     => 900,
		other => Reserved for future expansion.

29 - height
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => length or percentage follows
				bits 0-6: MBZ
			bit 7 clear => keywords:
				bits 0-6: 0000000 => auto,
					  other   => rffe.

2a - left
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => length or percentage follows
				bits 0-6: MBZ
			bit 7 clear => keywords:
				bits 0-6: 0000000 => auto,
					  other   => rffe.

2b - letter-spacing
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => length follows
				bits 0-6: MBZ
			bit 7 clear => keywords:
				bits 0-6: 0000000 => normal,
					  other   => rffe.

2c - line-height
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => length/number/percentage follows
				bits 0-6: 0000000 => number,
					  0000001 => dimension,
					  other   => rffe.
			bit 7 clear => keywords:
				bits 0-6: 0000000 => normal,
					  other   => rffe.

2d - list-style-image
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => string follows
				bits 0-6: MBZ
			bit 7 clear => keywords:
				bits 0-6: 0000000 => none,
					  other   => rffe.

2e - list-style-position
	<value> (14bits) :
		0     => inside,
		1     => outside,
		other => Reserved for future expansion.

2f - list-style-type
	<value> (14bits) :
		bits 6-13: MBZ
		bits 0-5 :
			0     => disc,
			1     => circle,
			2     => square,
			3     => decimal,
			4     => decimal-leading-zero,
			5     => lower-roman,
			6     => upper-roman,
			7     => lower-greek,
			8     => lower-latin,
			9     => upper-latin,
			a     => armenian,
			b     => georgian,
			c     => lower-alpha,
			d     => upper-alpha,
			e     => none,
			other => Reserved for future expansion.

30 - margin-top
31 - margin-right
32 - margin-bottom
33 - margin-left
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => length or percentage follows
				bits 0-6: MBZ
			bit 7 clear => keywords:
				bits 0-6: 0000000 => auto,
					  other   => rffe.

34 - max-height
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => length or percentage follows
				bits 0-6: MBZ
			bit 7 clear => keywords:
				bits 0-6: 0000000 => none,
					  other   => rffe.

35 - max-width
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => length or percentage follows
				bits 0-6: MBZ
			bit 7 clear => keywords:
				bits 0-6: 0000000 => none,
					  other   => rffe.

36 - min-height
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7: set => length or percentage follows
					bits 0-6: MBZ
			bit 7 clear => keywords:
				bits 0-6: 0000000 => auto,
					  other   => rffe.

37 - min-width
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7: set => length or percentage follows
					bits 0-6: MBZ
			bit 7 clear => keywords:
				bits 0-6: 0000000 => auto,
					  other   => rffe.

38 - orphans
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7: set => integer follows
					bits 0-6: MBZ
			       clear => Reserved for future expansion
			       		bits 0-6: MBZ

39 - outline-color
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => colour follows
				bits 0-6: MBZ
			bit 7 clear => keywords:
				bits 0-6: 0000000 => transparent,
					  0000001 => currentColor,
					  0000010 => invert,
					  other   => rffe.

3a - outline-style
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			00000000 => none,
			00000001 => hidden,
			00000010 => dotted,
			00000011 => dashed,
			00000100 => solid,
			00000101 => double,
			00000110 => groove,
			00000111 => ridge,
			00001000 => inset,
			00001001 => outset,
			other    => Reserved for future expansion.

3b - outline-width
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => length follows
				bits 0-6: MBZ
			bit 7 clear => keywords:
				bits 0-6: 0000000 => thin,
					  0000001 => medium,
					  0000010 => thick,
					  other   => rffe.

3c - overflow-x
	<value> (14bits) :
		0     => visible,
		1     => hidden,
		2     => scroll,
		3     => auto,
		other => Reserved for future expansion.

3d - padding-top
3e - padding-right
3f - padding-bottom
40 - padding-left
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7: set => length or percentage follows
					bits 0-6: MBZ
			       clear => Reserved for future expansion
			       		bits 0-6: MBZ

41 - page-break-after
	<value> (14bits) :
		0     => auto,
		1     => always,
		2     => avoid,
		3     => left,
		4     => right,
		other => Reserved for future expansion.

42 - page-break-before
	<value> (14bits) :
		0     => auto,
		1     => always,
		2     => avoid,
		3     => left,
		4     => right,
		other => Reserved for future expansion.

43 - page-break-inside
	<value> (14bits) :
		0     => auto,
		1     => avoid,
		other => Reserved for future expansion.

44 - pause-after
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7: set => time or percentage follows
					bits 0-6: MBZ
			       clear => Reserved for future expansion
			       		bits 0-6: MBZ

45 - pause-before
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7: set => time or percentage follows
					bits 0-6: MBZ
			       clear => Reserved for future expansion
			       		bits 0-6: MBZ

46 - pitch-range
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7: set => number follows
					bits 0-6: MBZ
			       clear => Reserved for future expansion
			       		bits 0-6: MBZ

47 - pitch
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => frequency follows
				bits 0-6: MBZ
			bit 7 clear => keywords:
				bits 0-6: 0000000 => x-low,
					  0000001 => low,
					  0000010 => medium,
					  0000011 => high,
					  0000100 => x-high,
					  other   => rffe.

48 - play-during
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => string follows
				bit 6: set => mix, clear => don't mix
				bit 5: set => repeat, clear => no repeat
				bits 0-4: MBZ
			bit 7 clear => keywords:
				bits 0-6: 0000000 => auto,
					  0000001 => none,
					  other   => rffe.

49 - position
	<value> (14bits) :
		0     => static,
		1     => relative,
		2     => absolute,
		3     => fixed,
		4     => sticky,
		other => Reserved for future expansion.

4a - quotes
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => two strings follow
				bits 0-6: MBZ
			bit 7 clear => keywords:
				bits 0-6: 0000000 => none,
					  other   => rffe.

	If the value indicates that strings are present, then there is a
	parameter list. Each item is preceded by a <value> word which declares
	the type of the next item. The list is terminated by a word with
	all bits clear (the encoding for "none").

4b - richness
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7: set => number follows
					bits 0-6: MBZ
			       clear => Reserved for future expansion
			       		bits 0-6: MBZ

4c - right
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => length or percentage follows
				bits 0-6: MBZ
			bit 7 clear => keywords:
				bits 0-6: 0000000 => auto,
					  other   => rffe.

4d - speak-header
	<value> (14bits) :
		0     => once,
		1     => always,
		other => Reserved for future expansion.

4e - speak-numeral
	<value> (14bits) :
		0     => digits,
		1     => continuous,
		other => Reserved for future expansion.

4f - speak-punctuation
	<value> (14bits) :
		0     => code,
		1     => none,
		other => Reserved for future expansion.

50 - speak
	<value> (14bits) :
		0     => normal,
		1     => none,
		2     => spell-out,
		other => Reserved for future expansion.

51 - speech-rate
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => number follows
				bits 0-6: MBZ
			bit 7 clear => keywords:
				bits 0-6: 0000000 => x-slow,
					  0000001 => slow,
					  0000010 => medium,
					  0000011 => fast,
					  0000100 => x-fast,
					  0000101 => faster,
					  0000110 => slower,
					  other   => rffe.

52 - stress
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7: set => number follows
					bits 0-6: MBZ
			       clear => Reserved for future expansion
			       		bits 0-6: MBZ

53 - table-layout
	<value> (14bits) :
		0     => auto,
		1     => fixed,
		other => Reserved for future expansion.

54 - text-align
	<value> (14bits) :
		0     => left,
		1     => right,
		2     => center,
		3     => justify,
		4     => -libcss-left,
		5     => -libcss-center,
		6     => -libcss-right,
		other => Reserved for future expansion.

55 - text-decoration
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			00000000 => none
			otherwise:
				bits 4-7: MBZ
				bit 3: set => blink, clear => no blink
				bit 2: set => line-through,
				       clear => no line-through
				bit 1: set => overline, clear => no overline
				bit 0: set => underline, clear => no underline

56 - text-indent
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7: set => length or percentage follows
					bits 0-6: MBZ
			       clear => Reserved for future expansion
			       		bits 0-6: MBZ

57 - text-transform
	<value> (14bits) :
		0     => capitalize,
		1     => uppercase,
		2     => lowercase,
		3     => none,
		other => Reserved for future expansion.

58 - top
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => length or percentage follows
				bits 0-6: MBZ
			bit 7 clear => keywords:
				bits 0-6: 0000000 => auto,
					  other   => rffe.

59 - unicode-bidi
	<value> (14bits) :
		0     => normal,
		1     => embed,
		2     => bidi-override,
		other => Reserved for future expansion.

5a - vertical-align
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => length or percentage follows
				bits 0-6: MBZ
			bit 7 clear => keywords:
				bits 0-6: 0000000 => baseline,
					  0000001 => sub,
					  0000010 => super,
					  0000011 => top,
					  0000100 => text-top,
					  0000101 => middle,
					  0000110 => bottom,
					  0000111 => text-bottom,
					  other   => rffe.

5b - visibility
	<value> (14bits) :
		0     => visible,
		1     => hidden,
		2     => collapse,
		other => Reserved for future expansion.

5c - voice-family
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => data follows
				bits 1-6: MBZ
				bit  0  : clear => string,
					  set => ident list (as string)
			bit 7 clear => keywords:
				bits 0-6: 0000000 => Reserved.
					  0000001 => male,
					  0000010 => female,
					  0000011 => child,
					  other   => rffe.

	In all cases, there is a parameter list. Each item is preceded by a
	<value> word which declares the type of the next item. The list is
	terminated by a word with all bits clear.

5d - volume
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => number/percentage follows
				bits 0-6: 0000000 => number,
					  0000001 => dimension,
					  other   => rffe.
			bit 7 clear => keywords:
				bits 0-6: 0000000 => silent,
					  0000001 => x-soft,
					  0000010 => soft,
					  0000011 => medium,
					  0000100 => loud,
					  0000101 => x-loud,
					  other   => rffe.

5e - white-space
	<value> (14bits) :
		0     => normal,
		1     => pre,
		2     => nowrap,
		3     => pre-wrap,
		4     => pre-line,
		other => Reserved for future expansion.

5f - widows
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7: set => integer follows
					bits 0-6: MBZ
			       clear => Reserved for future expansion
			       		bits 0-6: MBZ

60 - width
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => length or percentage follows
				bits 0-6: MBZ
			bit 7 clear => keywords:
				bits 0-6: 0000000 => auto,
					  other   => rffe.

61 - word-spacing
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => length follows
				bits 0-6: MBZ
			bit 7 clear => keywords:
				bits 0-6: 0000000 => normal,
					  other   => rffe.

62 - z-index
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => integer follows
				bits 0-6: MBZ
			bit 7 clear => keywords:
				bits 0-6: 0000000 => auto,
					  other   => rffe.

63 - opacity
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => number follows
				bits 0-6: MBZ
			bit 7 clear => Reserved for future expansion
				bits 0-6: MBZ

64 - break-after
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			00000000 => auto,
			00000001 => always,
			00000010 => avoid,
			00000011 => left,
			00000100 => right,
			00000101 => page,
			00000110 => column,
			00000111 => avoid-page,
			00001000 => avoid-column,
			other    => Reserved for future expansion.

65 - break-before
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			00000000 => auto,
			00000001 => always,
			00000010 => avoid,
			00000011 => left,
			00000100 => right,
			00000101 => page,
			00000110 => column,
			00000111 => avoid-page,
			00001000 => avoid-column,
			other    => Reserved for future expansion.

66 - break-inside
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			00000000 => auto,
			00000001 => avoid,
			00000010 => avoid-page,
			00000011 => avoid-column,
			other    => Reserved for future expansion.

67 - column-count
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => integer follows
				bits 0-6: MBZ
			bit 7 clear => keywords:
				bits 0-6: 0000000 => auto,
					  other   => rffe.

68 - column-fill
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			00000000 => balance,
			00000001 => auto,
			other    => Reserved for future expansion.

69 - column-gap
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => length follows
				bits 0-6: MBZ
			bit 7 clear => keywords:
				bits 0-6: 0000000 => normal,
					  other   => rffe.

6a - column-rule-color
	<value> (14bits) :
		bits 8-13: MBZ
		bit 7 set => colour follows.
			bits 0-6: MBZ
		bit 7 clear => Reserved for future expansion
			bits 0-6: MBZ

6b - column-rule-style
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			00000000 => none,
			00000001 => hidden,
			00000010 => dotted,
			00000011 => dashed,
			00000100 => solid,
			00000101 => double,
			00000110 => groove,
			00000111 => ridge,
			00001000 => inset,
			00001001 => outset,
			other    => Reserved for future expansion.

6c - column-rule-width
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => length follows
				bits 0-6: MBZ
			bit 7 clear => keywords:
				bits 0-6: 0000000 => thin,
					  0000001 => medium,
					  0000010 => thick,
					  other   => rffe.

6d - column-span
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			00000000 => none,
			00000001 => all,
			other    => Reserved for future expansion.

6e - column-width
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => length follows
				bits 0-6: MBZ
			bit 7 clear => keywords:
				bits 0-6: 0000000 => auto,
					  other   => rffe.

6f - writing-mode
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			00000000 => horizontal-tb,
			00000001 => vertical-rl,
			00000010 => vertical-lr,
			other    => Reserved for future expansion.

70 - overflow-y
	<value> (14bits) :
		0     => visible,
		1     => hidden,
		2     => scroll,
		3     => auto,
		other => Reserved for future expansion.

71 - box-sizing
	<value> (14bits) :
		0     => content-box,
		1     => border-box,
		other => Reserved for future expansion.

72 - align-content
	<value> (14bits) :
		0     => stretch,
		1     => flex-start,
		2     => flex-end,
		3     => center,
		4     => space-between,
		5     => space-around,
		6     => space-evenly
		other => Reserved for future expansion.

73 - align-items
	<value> (14bits) :
		0     => stretch,
		1     => flex-start,
		2     => flex-end,
		3     => center,
		4     => baseline,
		other => Reserved for future expansion.

74 - align-self
	<value> (14bits) :
		0     => stretch,
		1     => flex-start,
		2     => flex-end,
		3     => center,
		4     => baseline,
		5     => auto,
		other => Reserved for future expansion.

75 - flex-basis
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => length or percentage follows
				bits 0-6: MBZ
			bit 7 clear => keywords:
				bits 0-6:
					0000000 => auto,
					0000001 => content,
					other   => rffe.

76 - flex-direction
	<value> (14bits) :
		0     => row,
		1     => row-reverse,
		2     => column,
		3     => column-reverse,
		other => Reserved for future expansion.

77 - flex-grow
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => number follows
				bits 0-6: MBZ
			bit 7 clear => Reserved for future expansion
				bits 0-6: MBZ

78 - flex-shrink
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => number follows
				bits 0-6: MBZ
			bit 7 clear => Reserved for future expansion
				bits 0-6: MBZ

79 - flex-wrap
	<value> (14bits) :
		0     => nowrap,
		1     => wrap,
		2     => wrap-reverse,
		other => Reserved for future expansion.

7a - justify-content
	<value> (14bits) :
		0     => flex-start,
		1     => flex-end,
		2     => center,
		3     => space-between,
		4     => space-around,
		5     => space-evenly,
		other => Reserved for future expansion.

7b - order
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => integer follows
				bits 0-6: MBZ
			bit 7 clear => Reserved for future expansion
				bits 0-6: MBZ

7c - fill-opacity
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => number follows
				bits 0-6: MBZ
			bit 7 clear => Reserved for future expansion
				bits 0-6: MBZ

7d - stroke-opacity
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => number follows
				bits 0-6: MBZ
			bit 7 clear => Reserved for future expansion
				bits 0-6: MBZ

7e - background-size
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => size given
				bit 6: MBZ
				bit 5: set => height length follows
				       clear => height is auto
				bit 4: set => width length follows
				       clear => width is auto
				bits 0-3: MBZ
			bit 7 clear => keyword
				bits 0-6:
					0000001 => cover
					0000010 => contain
					other   => Reserved for future expansion

	If both lengths follow, the width comes first.

7f - box-shadow
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => shadows follow
				bits 0-6: MBZ
			bit 7 clear => keyword
				bits 0-6:
					0000000 => none
					other   => Reserved for future expansion

	The shadows are a count, then for each shadow ten values: flags
	(bit 0 set => inset, bit 1 set => current colour), the x and y
	offsets, the blur radius and the spread distance (each a length
	and unit) and the colour.

80 - border-top-left-radius
81 - border-top-right-radius
82 - border-bottom-right-radius
83 - border-bottom-left-radius
	<value> (14bits) :
		bits 1-13: MBZ
		bit 0 set => radii follow
		bit 0 clear => reserved for future expansion

	The radii are the horizontal radius then the vertical radius, each
	a length and unit, where the unit may be a percentage.  The
	shorthand, border-radius, is parsed into these four.

84 - overflow-wrap
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			00000000 => normal,
			00000001 => break-word,
			00000010 => anywhere,
			other    => Reserved for future expansion.

	The legacy name, word-wrap, is parsed into this property.

85 - word-break
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			00000000 => normal,
			00000001 => break-all,
			00000010 => keep-all,
			00000011 => break-word,
			other    => Reserved for future expansion.

86 - transform
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			00000000 => none,
			00000001 => translation follows,
			other    => Reserved for future expansion.

	The translation is the horizontal distance then the vertical
	distance, each a length and unit, where the unit may be a
	percentage of the box's own size.  Only the translation which the
	transform functions make is kept: translate(), translateX(),
	translateY(), translate3d() and the translation of matrix() and
	matrix3d().  Other functions are accepted and ignored.

87 - grid-template-columns
88 - grid-template-rows
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => track list follows:
				bits 0-6: MBZ
			bit 7 clear =>
				0000000 => none,
				other   => Reserved for future expansion.

	The track list is the number of entries, then six words per
	entry: the kind of entry, flags and the minimum and maximum
	sizing functions (one byte each, from the least significant;
	see css_computed_grid_track); the minimum value and unit; the
	maximum value and unit; and a string table index for a line
	name, or 0.  repeat() with a count is expanded.  The entries of
	repeat(auto-fill, ...) or repeat(auto-fit, ...) appear once,
	flagged.

89 - grid-template-areas
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => areas follow:
				bits 0-6: MBZ
			bit 7 clear =>
				0000000 => none,
				other   => Reserved for future expansion.

	The areas are the number of areas, the number of rows and the
	number of columns, then five words per area: a string table
	index for its name, its first and last row lines and its first
	and last column lines, numbered from 1.

8a - grid-auto-columns
8b - grid-auto-rows
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => track list follows, as for
				grid-template-columns, without names:
				bits 0-6: MBZ
			bit 7 clear =>
				0000000 => auto,
				other   => Reserved for future expansion.

8c - grid-auto-flow
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			00000000 => row,
			00000001 => column,
			00000010 => row dense,
			00000011 => column dense,
			other    => Reserved for future expansion.

8d - grid-column-start
8e - grid-column-end
8f - grid-row-start
90 - grid-row-end
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => a string table index for a name
				follows the integer
			bits 0-6 :
				0000000 => auto,
				0000001 => line, integer follows,
				0000010 => span, integer follows,
				other   => Reserved for future expansion.

	An integer of 0 means the line is given only by its name.

91 - row-gap
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			bit 7 set => length follows:
				bits 0-6: MBZ
			bit 7 clear =>
				0000000 => normal,
				other   => Reserved for future expansion.

92 - justify-items
93 - justify-self
	<value> (14bits) :
		bits 8-13: MBZ
		bits 0-7 :
			00000000 => stretch,
			00000001 => start,
			00000010 => end,
			00000011 => center,
			00000100 => baseline,
			00000101 => auto (justify-self only),
			other    => Reserved for future expansion.

94-3fe - Reserved for future expansion.

3ff - custom property (--name: value)
	<value> (14bits) : MBZ
	Followed by two string table indices: the name and the text of
	the value.
```
