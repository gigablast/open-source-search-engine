/* This is "plotter.h", the public header file for the C++ Plotter class
   provided by GNU libplotter, a shared class library for 2-dimensional
   vector graphics.  

   This file should be included by any code that uses the Plotter class.
   If the Plotter class was installed without X Window System support, be
   sure to do "#define X_DISPLAY_MISSING" before including this file.

   From the base Plotter class, the BitmapPlotter, MetaPlotter, TekPlotter,
   ReGISPlotter, HPGLPlotter, FigPlotter, CGMPlotter, PSPlotter, AIPlotter,
   SVGPlotter, GIFPlotter, PNMPlotter, PNGPlotter, and XDrawablePlotter
   classes are derived.  The PNMPlotter and PNGPlotter classes are derived
   from the BitmapPlotter class, the PCLPlotter class is derived from the
   HPGLPlotter class, and the XPlotter class is derived from the
   XDrawablePlotter class. */

/* If NOT_LIBPLOTTER is defined, this file magically becomes an internal
   header file used in GNU libplot, the C version of libplotter.  libplot
   has its own public header file, called "plot.h". */

/* Implementation note: In libplot, a Plotter is a typedef'd structure
   rather than a class instance.  Because of the need to support both
   libplot and libplotter, i.e. C and C++, all data members of derived
   Plotter classes are declared in this file twice: once in the Plotter
   structure (for C), and once in each derived class declaration (for C++). */

#ifndef _PLOTTER_H_
#define _PLOTTER_H_ 1

/***********************************************************************/

/* Version of GNU libplot/libplotter which this header file accompanies.
   This information is included beginning with version 4.0.

   The PL_LIBPLOT_VER_STRING macro is compiled into the library, as
   `pl_libplot_ver'.  The PL_LIBPLOT_VER macro is not compiled into it.
   Both are available to applications that include this header file. */

#define PL_LIBPLOT_VER_STRING "4.1"
#define PL_LIBPLOT_VER         401

extern const char pl_libplot_ver[8];   /* need room for 99.99aa */

/***********************************************************************/

/* Support ancient C compilers, best not explained further. */
#ifndef void
#define voidptr_t void *
#endif

/* If we're supporting X, include X-related header files needed by the
   class definition. */
#ifndef X_DISPLAY_MISSING
#include <X11/Xlib.h>
#include <X11/Intrinsic.h>
#endif /* not X_DISPLAY_MISSING */

/* Include iostream, stdio support if this is libplotter rather than
   libplot. */
#ifndef NOT_LIBPLOTTER
#include <iostream.h>
#include <stdio.h>
#endif

/* THE GLOBAL VARIABLES IN GNU LIBPLOTTER */
/* There are two; both are user-settable error handlers. */
#ifndef NOT_LIBPLOTTER
extern int (*libplotter_warning_handler) (const char *msg);
extern int (*libplotter_error_handler) (const char *msg);
#endif


/***********************************************************************/

/* Structures for points. */

typedef struct
{
  double x, y;
} plPoint;

typedef struct
{
  int x, y;
} plIntPoint;

/* Structures for paths, as painted by any Plotter. */

/* Normally, a simple path is a sequence of contiguous line segments,
   circular arc segments, elliptic arc segments, quadratic Bezier segments,
   or cubic Bezier segments.  All Plotters support line segments, but many
   Plotters don't support the other types of segment.  Some Plotters also
   support (closed) simple paths that are single circles, ellipses, or
   "boxes" (rectangles aligned with the coordinate axes).

   A simple path that is a sequence of segments is represented internally
   as a list of plPathSegments.  Each contains a single endpoint (x,y), and
   specifies how to get there (e.g., via a pen-up motion, which is used for
   the first point in a path, or via a line segment, or a curve defined by
   control points).
   
   A well-formed simple path of this `segment list' type has the form:
   { moveto { line | arc | ellarc | quad | cubic }* { closepath }? } */

/* Allowed values for the path segment type field. */
typedef enum 
{ 
  S_MOVETO, S_LINE, S_ARC, S_ELLARC, S_QUAD, S_CUBIC, S_CLOSEPATH
} plPathSegmentType;

/* Structure for a path segment. */
typedef struct
{
  plPathSegmentType type;
  plPoint p;			/* endpoint of segment */
  plPoint pc;			/* intermediate control point (if any) */
  plPoint pd;			/* additional control point (S_CUBIC only) */
} plPathSegment;

/* Allowed values for the path type field in a plPath (see below). */
typedef enum 
{ 
  PATH_SEGMENT_LIST,		/* the default kind */
  PATH_CIRCLE, PATH_ELLIPSE, PATH_BOX /* supported by some Plotters */
} plPathType;

/* Structure for a simple path.  The default kind of simple path is a list
   of segments.  However, a simple path may also be a circle, ellipse, or
   box, which some Plotter support as primitive drawing elements, at least
   under some circumstances.  Other Plotters automatically flatten these
   built-in primitives into segment lists.  The advisory `primitive' flag
   is set to `true' to indicate that a segment list is in fact such a
   flattened object. */

typedef struct
{
  plPathType type;	/* PATH_{SEGMENT_LIST,CIRCLE,ELLIPSE,BOX} */
  double llx, lly, urx, ury;    /* bounding box */
  /* simple path of segment list type */
  plPathSegment *segments;	/* list of path segments */
  int num_segments;		/* number of segments in list */
  int segments_len;		/* length of buffer for list storage (bytes) */
  bool primitive;		/* advisory (see above; some Plotters use it)*/
  /* simple path of built-in primitive type (circle/ellipse/box) */
  plPoint pc;			/* CIRCLE/ELLIPSE: center */
  double radius;		/* CIRCLE: radius */
  double rx, ry;		/* ELLIPSE: semi-axes */
  double angle;			/* ELLIPSE: angle of first axis */
  plPoint p0, p1;		/* BOX: opposite vertices */
  bool clockwise;		/* CIRCLE/ELLIPSE/BOX: clockwise? */
} plPath;

/* An integer counterpart to plPathSegment, used by some Plotters during
   the mapping of segment-list paths to the device frame.  This shouldn't
   be defined here, since it's so Plotter-specific. */
typedef struct
{
  plPathSegmentType type;
  plIntPoint p;			/* endpoint of segment */
  plIntPoint pc;		/* intermediate control point (if any) */
  plIntPoint pd;		/* additional control point (S_CUBIC only) */
  double angle;			/* subtended angle (for S_ARC, if used) */
} plIntPathSegment;

/* Values for the parameters `allowed_{arc|ellarc|quad|cubic}_scaling' of
   any Plotter.  Those parameters specify which sorts of user frame ->
   device frame affine transformation are allowed, if an arc or Bezier that
   is part of a path is to be placed in the path buffer's segment list as a
   single segment, rather than approximated as a polyline.

   These are also the possible values for the parameters
   allowed_{circle|ellipse|box}_scaling' of any Plotter, which specify
   whether any circle/ellipse/box should be placed in the path buffer as a
   primitive, rather than split into arc segments or line segments and
   placed in the segment list.

   The reason for extensively parametrizing the internal operation of any
   Plotter (i.e. restricting what gets placed in its path buffer) is that
   for many Plotters, the Plotter-specific operation _paint_path(), which
   is called by endpath(), can't handle arbitrary drawing primitives,
   because the Plotter's output format doesn't support them.

   The values AS_UNIFORM and AS_AXES_PRESERVED aren't used for Beziers, but
   they're used for the other primitives.  Also, insofar as ellipses go, a
   value of AS_AXES_PRESERVED for the allowed scaling is intepreted as
   meaning that not only should the affine map preserve coordinate axes,
   but the ellipse itself, to be placed in the path buffer, should have its
   major and minor axes aligned with the coordinate axes.  See g_ellipse.c.  */

typedef enum 
{ 
  AS_NONE,			/* primitive not supported at all */
  AS_UNIFORM,			/* supported only if transf. is uniform  */
  AS_AXES_PRESERVED,		/* supported only if transf. preserves axes */
  AS_ANY			/* supported irrespective of transformation */
} plScalingType;


/**********************************************************************/

/* Structures for colors (we don't fully distinguish between 24-bit color
   and 48-bit color, though we should). */

/* RGB */
typedef struct
{
  int red;
  int green;
  int blue;
} plColor;

/* BitmapPlotters (including PNMPlotters and PNGPlotters, which are
   derived) and GIFPlotters use the libxmi scan-conversion library, which
   is compiled into libplot/libplotter.  libxmi writes into a pixmap that
   is made up of the following type of pixel.  We use a struct containing a
   union, so that the compiled-in libxmi can be used both by GIF Plotters
   (in which pixel values are color indices) and by BitmapPlotters (in
   which pixel values are 24-bit RGB values).  We distinguish them by the
   `type' field. */
#define MI_PIXEL_TYPE struct \
{ \
  unsigned char type; \
  union \
    { \
      unsigned char index; \
      unsigned char rgb[3]; \
    } u; \
}

/* values for the `type' field */
#define MI_PIXEL_INDEX_TYPE 0
#define MI_PIXEL_RGB_TYPE 1

#define MI_SAME_PIXEL(pixel1,pixel2) \
  (((pixel1).type == MI_PIXEL_INDEX_TYPE \
    && (pixel2).type == MI_PIXEL_INDEX_TYPE \
    && (pixel1).u.index == (pixel2).u.index) \
   || \
  ((pixel1).type == MI_PIXEL_RGB_TYPE \
    && (pixel2).type == MI_PIXEL_RGB_TYPE \
    && (pixel1).u.rgb[0] == (pixel2).u.rgb[0] \
    && (pixel1).u.rgb[1] == (pixel2).u.rgb[1] \
    && (pixel1).u.rgb[2] == (pixel2).u.rgb[2]))


/**********************************************************************/

/* Structure used for characterizing a page type (e.g. "letter", "a4"; see
   our database of known page types in g_pagetype.h).  Any Plotter includes
   a pointer to one of these.

   For all `physical' Plotters, i.e. those with a page type determined by
   the PAGESIZE parameter, we map the window that the user specifies by
   invoking space(), to a viewport whose default size is fixed and
   Plotter-independent.  E.g., for any Plotter for which PAGESIZE is
   "letter", the default viewport is a square of size 8.0in x 8.0in.

   All physical Plotters position this default viewport at the center of
   the page, except that HPGLPlotters don't know exactly where the origin
   of the device coordinate system is.  PCLPlotters do, though, when
   they're emitting HP-GL/2 code (there's a field in the struct that
   specifies that, see below).  See comments in g_pagetype.h. */

typedef struct
{
  const char *name;		/* official name, e.g. "a" */
  const char *alt_name;		/* alternative name if any, e.g. "letter" */
  const char *fig_name;		/* name used in Fig format (case-sensitive) */
  bool metric;			/* metric vs. Imperial, advisory only */
  double xsize, ysize;		/* width, height in inches */
  double default_viewport_size;	/* size of default square viewport, in inches*/
  double pcl_hpgl2_xorigin;	/* origin for HP-GL/2-in-PCL5 plotting */
  double pcl_hpgl2_yorigin;  
  double hpgl2_plot_length;	/* plot length (for HP-GL/2 roll plotters) */
} plPageData;

/* Structure in which the user->NDC and user->device affine coordinate
   transformations are stored.  Any drawing state includes one of these
   structures.  The user->NDC transformation is a bit more fundamental,
   since it's used as an attribute of all objects that get drawn.

   The user->device transformation is the product of the user->NDC
   transformation and the NDC->device transformation (stored in the Plotter
   itself, since it never changes after being initialized at Plotter
   creation time).  The reason we precompute the user->device
   transformation and store it here is that we use it so frequently. */

typedef struct
{ 
  /* the user->NDC transformation */
  double m_user_to_ndc[6];	/* 1. a linear transformation (4 elements) */
  				/* 2. a translation (2 elements) */
  /* the user->device transformation, precomputed for convenience */
  double m[6];
  /* data on user->device map, also precomputed for convenience */
  bool uniform;			/* transf. scaling is uniform? */
  bool axes_preserved;		/* transf. preserves axis directions? */
  bool nonreflection;		/* transf. doesn't involve a reflection? */
} plTransform;


/**********************************************************************/

/* Drawing state structure.  Includes drawing attributes, and the state of
   any uncompleted path object.  When open, i.e., when drawing a page of
   graphics, any Plotter maintains a stack of these things.  Many of the
   data members are device-dependent, i.e., specific to individual derived
   Plotter classes, but it's more efficient to keep them here, in a single
   structure.  The device-independent data members are listed first. */

typedef struct plDrawStateStruct
{
/***************** DEVICE-INDEPENDENT PART ***************************/

/* graphics cursor position */
  plPoint pos;			/* graphics cursor position in user space */

/* affine transformation from user coordinates to normalized device
   coordinates, and also to actual device coordinates (precomputed) */
  plTransform transform;	/* see definition of structure above */

/* the compound path being drawn, if any */
  plPath *path;			/* simple path being drawn */
  plPath **paths;		/* previously drawn simple paths */
  int num_paths;		/* number of previously drawn simple paths */
  plPoint start_point;		/* starting point (used by closepath()) */

/* modal drawing attributes */
  /* 1. path-related attributes */
  const char *fill_rule;	/* fill rule */
  int fill_rule_type;		/* one of FILL_*, determined by fill rule */
  const char *line_mode;	/* line mode */
  int line_type;		/* one of L_*, determined by line mode */
  bool points_are_connected;	/* if not set, path displayed as points */
  const char *cap_mode;		/* cap mode */
  int cap_type;			/* one of CAP_*, determined by cap mode */
  const char *join_mode;	/* join mode */
  int join_type;		/* one of JOIN_*, determined by join mode */
  double miter_limit;		/* miter limit for line joins */
  double line_width;		/* width of lines in user coordinates */
  bool line_width_is_default;	/* line width is (Plotter-specific) default? */
  double device_line_width;	/* line width in device coordinates */
  int quantized_device_line_width; /* line width, quantized to integer */
  const double *dash_array;	/* array of dash on/off lengths (nonnegative)*/
  int dash_array_len;		/* length of same */
  double dash_offset;		/* offset distance into dash array (`phase') */
  bool dash_array_in_effect;	/* dash array should override line mode? */
  int pen_type;			/* pen type (0 = no pen, 1 = pen) */
  int fill_type;		/* fill type (0 = no fill, 1 = fill, ...) */
  int orientation;	        /* orientation of circles etc.(1=c'clockwise)*/
  /* 2. text-related attributes */
  const char *font_name;	/* font name */
  double font_size;		/* font size in user coordinates */
  bool font_size_is_default;	/* font size is (Plotter-specific) default? */
  double text_rotation;		/* degrees counterclockwise, for labels */
  const char *true_font_name;	/* true font name (as retrieved) */
  double true_font_size;	/* true font size (as retrieved) */
  double font_ascent;		/* font ascent (as retrieved) */
  double font_descent;		/* font descent (as retrieved) */
  double font_cap_height;	/* font capital height (as received) */
  int font_type;		/* F_{HERSHEY|POSTSCRIPT|PCL|STICK|OTHER} */
  int typeface_index;		/* typeface index (in g_fontdb.h table) */
  int font_index;		/* font index, within typeface */
  bool font_is_iso8859_1;	/* whether font uses iso8859_1 encoding */
  /* 3. color attributes (fgcolor and fillcolor are path-related; fgcolor
     affects other primitives too) */
  plColor fgcolor;		/* foreground color, i.e., pen color */
  plColor fillcolor_base;	/* fill color (not affected by fill_type) */
  plColor fillcolor;		/* fill color (takes fill_type into account) */
  plColor bgcolor;		/* background color for graphics display */
  bool bgcolor_suppressed;	/* no actual background color? */

/* default values for certain attributes, used when an out-of-range value
   is requested (these two are special because they're set by fsetmatrix()) */
  double default_line_width;	/* width of lines in user coordinates */
  double default_font_size;	/* font size in user coordinates */

/****************** DEVICE-DEPENDENT PART ***************************/

/* elements specific to the HPGL Plotter drawing state */
  double hpgl_pen_width;	/* pen width (frac of diag dist betw P1,P2) */

/* elements specific to the Fig Plotter drawing state */
  int fig_font_point_size;	/* font size in fig's idea of points */
  int fig_fill_level;		/* fig's fill level */
  int fig_fgcolor;		/* fig's foreground color */
  int fig_fillcolor;		/* fig's fill color */

/* elements specific to the PS Plotter drawing state */
  double ps_fgcolor_red;	/* RGB for fgcolor, each in [0.0,1.0] */
  double ps_fgcolor_green;
  double ps_fgcolor_blue;
  double ps_fillcolor_red;	/* RGB for fillcolor, each in [0.0,1.0] */
  double ps_fillcolor_green;
  double ps_fillcolor_blue;
  int ps_idraw_fgcolor;		/* index of idraw fgcolor in table */
  int ps_idraw_bgcolor;		/* index of idraw bgcolor in table */
  int ps_idraw_shading;		/* index of idraw shading in table */

/* elements specific to the GIF Plotter drawing state */
  plColor i_pen_color;		/* pen color (24-bit RGB) */
  plColor i_fill_color;		/* fill color (24-bit RGB) */
  plColor i_bg_color;		/* background color (24-bit RGB) */
  unsigned char i_pen_color_index; /* pen color index */
  unsigned char i_fill_color_index; /* fill color index */
  unsigned char i_bg_color_index; /* bg color index */
  bool i_pen_color_status;	/* foreground color index is genuine? */
  bool i_fill_color_status;	/* fill color index is genuine? */
  bool i_bg_color_status;	/* background color index is genuine? */

#ifndef X_DISPLAY_MISSING
/* elements specific to the X Drawable Plotter drawing state */
  double x_font_pixmatrix[4];	/* pixel matrix, parsed from font name */
  bool x_native_positioning;	/* if set, can use XDrawString() etc. */
  XFontStruct *x_font_struct;	/* font structure (used in x_text.c) */
  const unsigned char *x_label;	/* label (hint to _x_retrieve_font()) */
  GC x_gc_fg;			/* graphics context, for drawing */
  GC x_gc_fill;			/* graphics context, for filling */
  GC x_gc_bg;			/* graphics context, for erasing */
  plColor x_current_fgcolor;	/* pen color stored in GC (48-bit RGB) */
  plColor x_current_fillcolor;	/* fill color stored in GC (48-bit RGB) */
  plColor x_current_bgcolor;	/* bg color stored in GC (48-bit RGB) */
  unsigned long x_gc_fgcolor;	/* color stored in drawing GC (pixel value) */
  unsigned long x_gc_fillcolor;	/* color stored in filling GC (pixel value) */
  unsigned long x_gc_bgcolor;	/* color stored in erasing GC (pixel value) */
  bool x_gc_fgcolor_status;	/* pixel value in drawing GC is genuine? */
  bool x_gc_fillcolor_status;	/* pixel value in filling GC is genuine? */
  bool x_gc_bgcolor_status;	/* pixel value in erasing GC is genuine? */
  int x_gc_line_style;		/* line style stored in drawing GC */
  int x_gc_cap_style;		/* cap style stored in drawing GC */
  int x_gc_join_style;		/* join style stored in drawing GC */
  int x_gc_line_width;		/* line width stored in drawing GC */
  const char *x_gc_dash_list;	/* dash list stored in drawing GC */
  int x_gc_dash_list_len;	/* length of dash list stored in drawing GC */
  int x_gc_dash_offset;		/* offset into dash sequence, in drawing GC */
  int x_gc_fill_rule;		/* fill rule stored in filling GC */
#endif /* not X_DISPLAY_MISSING */

/* pointer to previous drawing state */
  struct plDrawStateStruct *previous;

} plDrawState;


/**********************************************************************/

/* An output buffer that may easily be resized.  Used by most Plotters that
   do not do real-time output, to store device code for all graphical
   objects plotted on a page, and page-specific data such as the bounding
   box and `fonts used' information.  (See e.g. g_outbuf.c.)  Plotters that
   wait until they are deleted before outputing graphics, e.g. PSPlotters
   and CGMPlotters, maintain not just one of these things but rather a
   linked list, one output buffer per page. */

/* NUM_PS_FONTS and NUM_PCL_FONTS should agree with the number of fonts of
   each type in g_fontdb.c.  These are also defined in libplot/extern.h. */
#define NUM_PS_FONTS 35
#define NUM_PCL_FONTS 45

typedef struct plOutbufStruct
{
  /* if non-NULL, a plOutbuf containing a page header */
  struct plOutbufStruct *header;

  /* if non-NULL, a plOutbuf containing a page trailer */
  struct plOutbufStruct *trailer;

  /* device code for the graphics on the page */
  char *base;			/* start of buffer */
  unsigned long len;		/* size of buffer */
  char *point;			/* current point (high-water mark) */
  char *reset_point;		/* point below which contents are frozen */
  unsigned long contents;	/* size of contents */
  unsigned long reset_contents;	/* size of frozen contents if any */

  /* page-specific information that some Plotters generate and use (this is
     starting to look like a Christmas tree...) */
  double xrange_min;		/* bounding box, in device coordinates */
  double xrange_max;
  double yrange_min;
  double yrange_max;
  bool ps_font_used[NUM_PS_FONTS]; /* PS fonts used on page */
  bool pcl_font_used[NUM_PCL_FONTS]; /* PCL fonts used on page */
  plColor bg_color;		/* background color for the page */
  bool bg_color_suppressed;	/* background color is "none"? */

  /* a hook for Plotters to hang other page-specific data */
  voidptr_t extra;

  /* pointer to previous Outbuf in page list if any */
  struct plOutbufStruct *next;
} plOutbuf;

/* Each Plotter caches the color names that have previously been mapped to
   RGB triples via libplot's colorname database (see g_colorname.h).
   For the cache, a linked list is currently used. */

typedef struct
{
  const char *name;
  unsigned char red;
  unsigned char green;
  unsigned char blue;
} plColorNameInfo;

typedef struct plCachedColorNameInfoStruct
{
  const plColorNameInfo *info;
  struct plCachedColorNameInfoStruct *next;
} plCachedColorNameInfo;

typedef struct
{
  plCachedColorNameInfo *cached_colors;	/* head of linked list */
} plColorNameCache;

#ifndef X_DISPLAY_MISSING
/* Each X DrawablePlotter (or X Plotter) keeps track of which fonts have
   been request from an X server, in any connection, by constructing a
   linked list of these records.  A linked list is good enough if we don't
   have huge numbers of font changes. */
typedef struct plFontRecordStruct
{
  char *name;			/* font name, preferably an XLFD name */
  XFontStruct *x_font_struct;	/* font structure */
  double true_font_size;
  double font_pixmatrix[4];
  double font_ascent;
  double font_descent;
  double font_cap_height;
  bool native_positioning;
  bool font_is_iso8859_1;
  bool subset;			/* did we retrieve a subset of the font? */
  unsigned char subset_vector[32]; /* 256-bit vector, 1 bit per font char */
  struct plFontRecordStruct *next; /* most recently retrieved font */
} plFontRecord;

/* Allocated color cells are kept track of similarly */
typedef struct plColorRecordStruct
{
  XColor rgb;			/* RGB value and pixel value (if any) */
  bool allocated;		/* pixel value successfully allocated? */
  int frame_number;		/* frame that cell was most recently used in*/
  int page_number;		/* page that cell was most recently used in*/
  struct plColorRecordStruct *next; /* most recently retrieved color cell */
} plColorRecord;
#endif /* not X_DISPLAY_MISSING */


/***********************************************************************/

/* The Plotter class, and also its derived classes (in libplotter).  In
   libplot, a Plotter is a struct, and there are no derived classes; the
   data members of the derived classes are located inside the Plotter
   structure. */

/* A few miscellaneous constants that appear in the declaration of the
   Plotter class (should be moved elsewhere if possible). */

/* Number of recognized Plotter parameters (see g_params2.c). */
#define NUM_PLOTTER_PARAMETERS 33

/* Maximum number of pens, or logical pens, for an HP-GL/2 device.  Some
   such devices permit as many as 256, but all should permit at least 32.
   Our pen numbering will range over 0..HPGL2_MAX_NUM_PENS-1. */
#define HPGL2_MAX_NUM_PENS 32

/* Maximum number of non-builtin colors that can be specified in an xfig
   input file.  See also FIG_NUM_STD_COLORS, defined in
   libplot/extern.h. */
#define FIG_MAX_NUM_USER_COLORS 512

/* Supported Plotter types.  These values are used in a `tag field', in
   libplot but not libplotter.  (C++ doesn't have such things, at least it
   didn't until RTTI was invented :-)). */
#ifdef NOT_LIBPLOTTER
typedef enum 
{
  PL_GENERIC,			/* instance of base Plotter class */
  PL_BITMAP,			/* bitmap class, derived from by PNM and PNG */
  PL_META,			/* GNU graphics metafile */
  PL_TEK,			/* Tektronix 4014 with EGM */
  PL_REGIS,			/* ReGIS (remote graphics instruction set) */
  PL_HPGL,			/* HP-GL and HP-GL/2 */
  PL_PCL,			/* PCL 5 (i.e. HP-GL/2 w/ header, trailer) */
  PL_FIG,			/* xfig 3.2 */
  PL_CGM,			/* CGM (Computer Graphics Metafile) */
  PL_PS,			/* Postscript, with idraw support */
  PL_AI,			/* Adobe Illustrator 5 (or 3) */
  PL_SVG,			/* Scalable Vector Graphics */
  PL_GIF,			/* GIF 87a or 89a */
  PL_PNM			/* Portable Anymap Format (PBM/PGM/PPM) */
#ifdef INCLUDE_PNG_SUPPORT
  , PL_PNG			/* PNG: Portable Network Graphics */
#endif
#ifndef X_DISPLAY_MISSING
  , PL_X11_DRAWABLE		/* X11 Drawable */
  , PL_X11			/* X11 (pops up, manages own window[s]) */
#endif
} plPlotterTag;
#endif /* NOT_LIBPLOTTER */

/* Types of Plotter output model.  The `output_model' data element in any
   Plotter class specifies the output model, and the libplot machinery
   takes it from there.  In particular, plOutbuf structures (one per page)
   are maintained if necessary, and written out at the appropriate time.

   Note: in the PAGES_ALL_AT_ONCE, OUTPUT_VIA_CUSTOM_ROUTINES* cases, the
   Plotter is responsible for managing its own output.  In the
   PAGES_ALL_AT_ONCE case, libplot maintains not just one but an entire
   linked list of plOutbuf's, to be scanned over by the derived Plotter at
   deletion time. */

typedef enum 
{
  PL_OUTPUT_NONE,
  /* No output at all; Plotter is used primarily for subclassing.  E.g.,
     generic and Bitmap Plotters. */

  PL_OUTPUT_ONE_PAGE,
  /* Plotter produces at most one page of output (the first), and uses
     libplot's builtin plOutbuf-based output mechanism.  The first page,
     which is written to a plOutbuf, is written out by the first invocation
     of closepl(), and all later pages are ignored.  E.g., Fig,
     Illustrator, and SVG Plotters. */

  PL_OUTPUT_ONE_PAGE_AT_A_TIME,
  /* Plotter produces any number of pages of output, and uses libplot's
     builtin plOutbuf-based output mechanism.  Each page is written out as
     soon as closepl() is called on it.  E.g., HP-GL/PCL Plotters.  */

  PL_OUTPUT_PAGES_ALL_AT_ONCE,
  /* Plotter produces any number of pages of output, and uses libplot's
     builtin plOutbuf-based output mechanism.  But pages are written out
     only when the Plotter is deleted, i.e., when the internal terminate()
     function is called.  (Actually, it's the generic-class terminate(),
     which any derived-class terminate() calls, that does it.)  Because all
     pages need to be stored, a linked list of plOutbuf's is created, one
     per page.  E.g., PS and CGM Plotters.  */

  PL_OUTPUT_VIA_CUSTOM_ROUTINES,
  /* Plotter uses its own output routines to write one or possibly more
     pages to its output stream, as whole pages (i.e., when closepl() is
     called).  It doesn't use libplot's plOutbuf-based output routines.
     E.g., PNM, GIF, and PNG Plotters (all of which output only 1 page). */

  PL_OUTPUT_VIA_CUSTOM_ROUTINES_IN_REAL_TIME,
  /* Plotter uses its own output routines to write to its output stream, in
     real time.  It doesn't use libplot's plOutbuf-based output routines.
     E.g., Metafile, Tektronix, and ReGIS Plotters. */

  PL_OUTPUT_VIA_CUSTOM_ROUTINES_TO_NON_STREAM
  /* Plotter uses its own output routines to write to something other than
     an output stream, which must presumably be passed to it as a Plotter
     parameter.  May or may not plot in real time.  E.g., X Drawable and X
     Plotters (both of which plot in real time). */
} plPlotterOutputModel;

/* Plotter data.  These data members of any Plotter object are stuffed into
   a single struct, for convenience.  Most of them are initialized by the
   initialize() method, and don't change thereafter.  So they're really
   parameters: they define the functioning of any Plotter.
   
   A few of these, e.g. the `page' member, do change at later times.  But
   it's the core Plotter code that changes them; not the device-specific
   drivers.  They're flagged by D: (i.e. dynamic), in their description. */

typedef struct
{
  /* data members (a great many!) which are really Plotter parameters */

#ifdef NOT_LIBPLOTTER
  /* tag field */
  plPlotterTag type;		/* Plotter type: one of PL_* defined above */
#endif /* NOT_LIBPLOTTER */

  /* low-level I/O issues */
  plPlotterOutputModel output_model;/* one of PL_OUTPUT_* (see above) */
  FILE *infp;			/* stdio-style input stream if any */
  FILE *outfp;			/* stdio-style output stream if any */
  FILE *errfp;			/* stdio-style error stream if any */
#ifndef NOT_LIBPLOTTER
  istream *instream;		/* C++-style input stream if any */
  ostream *outstream;		/* C++-style output stream if any */
  ostream *errstream;		/* C++-style error stream if any */
#endif /* not NOT_LIBPLOTTER */

  /* device driver parameters (i.e., instance copies of class variables) */
  voidptr_t params[NUM_PLOTTER_PARAMETERS];

  /* (mostly) user-queryable capabilities: 0/1/2 = no/yes/maybe */
  int have_wide_lines;	
  int have_dash_array;
  int have_solid_fill;
  int have_odd_winding_fill;
  int have_nonzero_winding_fill;
  int have_settable_bg;
  int have_escaped_string_support; /* can plot labels containing escapes? */
  int have_ps_fonts;
  int have_pcl_fonts;
  int have_stick_fonts;
  int have_extra_stick_fonts;
  int have_other_fonts;

  /* text and font-related parameters (internal, not queryable by user) */
  int default_font_type;	/* F_{HERSHEY|POSTSCRIPT|PCL|STICK} */
  bool pcl_before_ps;		/* PCL fonts searched first? (if applicable) */
  bool have_horizontal_justification; /*device can justify text horizontally?*/
  bool have_vertical_justification; /* device can justify text vertically? */
  bool kern_stick_fonts;      /* device kerns variable-width HP vector fonts?*/
  bool issue_font_warning;	/* issue warning on font substitution? */

  /* path-related parameters (also internal) */
  int max_unfilled_path_length; /* user-settable, for unfilled polylines */
  bool have_mixed_paths;	/* can mix arcs/Beziers and lines in paths? */
  plScalingType allowed_arc_scaling; /* scaling allowed for circular arcs */
  plScalingType allowed_ellarc_scaling;	/* scaling allowed for elliptic arcs */
  plScalingType allowed_quad_scaling; /*scaling allowed for quadratic Beziers*/
  plScalingType allowed_cubic_scaling; /* scaling allowed for cubic Beziers */
  plScalingType allowed_box_scaling; /* scaling allowed for boxes */
  plScalingType allowed_circle_scaling; /* scaling allowed for circles */
  plScalingType allowed_ellipse_scaling; /* scaling allowed for ellipses */

  /* color-related parameters (also internal) */
  bool emulate_color;		/* emulate color by grayscale? */

  /* cache of previously retrieved color names (used for speed) */
  plColorNameCache *color_name_cache;/* pointer to color name cache */

  /* info on the device coordinate frame (ranges for viewport in terms of
     native device coordinates, etc.; note that if flipped_y=true, then
     jmax<jmin or ymax<ymin) */
  int display_model_type;	/* one of DISP_MODEL_{PHYSICAL,VIRTUAL} */
  int display_coors_type;	/* one of DISP_DEVICE_COORS_{REAL, etc.} */
  bool flipped_y;		/* y increases downward? */
  int imin, imax, jmin, jmax;	/* ranges, if virtual with integer coors */
  double xmin, xmax, ymin, ymax; /* ranges, if physical with real coors */

  /* low-level page and viewport information, if display is physical.
     Final six parameters are in terms of inches, and can be specified by
     setting the PAGESIZE Plotter parameter. */
  const plPageData *page_data;	/* page dimensions and other characteristics */
  double viewport_xsize, viewport_ysize; /* viewport dimensions (inches) */
  double viewport_xorigin, viewport_yorigin; /* viewport origin (inches) */
  double viewport_xoffset, viewport_yoffset; /* viewport origin offset */

  /* affine transformation from NDC to device coordinates */
  double m_ndc_to_device[6];	/*  1. a linear transformation (4 elements)
  				    2. a translation (2 elements) */

/* dynamic data members, which are updated during Plotter operation, unlike
   the many data members above */

  bool open;			/* D: whether or not Plotter is open */
  bool opened;			/* D: whether or not Plotter has been opened */
  int page_number;		/* D: number of times it has been opened */
  bool fontsize_invoked;	/* D: fontsize() invoked on this page? */
  bool linewidth_invoked;	/* D: linewidth() invoked on this page? */
  int frame_number;		/* D: number of frame in page */

  /* whether warning messages have been issued */
  bool font_warning_issued;	/* D: issued warning on font substitution */
  bool pen_color_warning_issued; /* D: issued warning on name substitution */
  bool fill_color_warning_issued; /* D: issued warning on name substitution */
  bool bg_color_warning_issued;	/* D: issued warning on name substitution */

  /* pointers to output buffers, containing graphics code */
  plOutbuf *page;		/* D: output buffer for current page */
  plOutbuf *first_page;		/* D: first page (if a linked list is kept) */

} plPlotterData;

/* The macro P___ elides argument prototypes if the compiler is a pre-ANSI
   C compiler that does not support them. */
#ifdef P___
#undef P___
#endif
#if defined (__STDC__) || defined (_AIX) \
	|| (defined (__mips) && defined (_SYSTYPE_SVR4)) \
	|| defined(WIN32) || defined(__cplusplus)
#define P___(protos) protos
#else
#define P___(protos) ()
#endif

/* The macro Q___ is used for declaring Plotter methods (as function
   pointers for libplot, and as function members of the Plotter
   class, for libplotter).  QQ___ is also used for declaring PlotterParams
   methods.  It is the same as Q___, but does not declare the function
   members as virtual (the PlotterParams class is not derived from). */
#ifdef Q___
#undef Q___
#endif
#ifdef NOT_LIBPLOTTER
#define Q___(rettype,f) rettype (*f)
#define QQ___(rettype,f) rettype (*f)
#else  /* LIBPLOTTER */
#define Q___(rettype,f) virtual rettype f
#define QQ___(rettype,f) rettype f
#endif

/* Methods of the Plotter class (and derived classes) all have a hidden
   argument, called `this', which is a pointer to the invoking Plotter
   instance.  That's a standard C++ convention.  In libplot, we must pass
   such a pointer explicitly, as an extra argument; we call it `_plotter'.
   In libplotter, each occurrence of `_plotter' in the body of a Plotter
   method is mapped to `this'.

   Since the same code is used for both libplot and libplotter, we use a
   macro, R___() or S___(), in the declaration and the definition of each
   Plotter method.  In libplotter, they elide their arguments.  But in
   libplot, they do not; also, R___() appends a comma.

   Methods of the PlotterParams helper class are handled similarly.  In
   libplot, each of them has an extra argument, `_plotter_params'.  This is
   arranged via R___() or S___().  In libplotter, each occurrence of
   `_plotter_params' in the body of a PlotterParams method is mapped to
   `this'. */

#ifdef NOT_LIBPLOTTER
#define R___(arg1) arg1,
#define S___(arg1) arg1
#else  /* LIBPLOTTER */
#define _plotter this
#define _plotter_params this
#define R___(arg1)
#define S___(arg1)
#endif

/* The PlotterParams class (or struct) definition.  This is a helper class.
   Any instance of it holds parameters that are used when instantiating the
   Plotter class.  */

#ifndef NOT_LIBPLOTTER
class PlotterParams
#else
typedef struct plPlotterParamsStruct /* this tag is used only by libplot */
#endif
{
#ifndef NOT_LIBPLOTTER
 public:
  /* PlotterParams CTORS AND DTOR; copy constructor, assignment operator */
  PlotterParams ();
  ~PlotterParams ();
  PlotterParams (const PlotterParams& oldPlotterParams);
  PlotterParams& operator= (const PlotterParams& oldPlotterParams);
#endif

  /* PLOTTERPARAMS PUBLIC METHODS */
  QQ___(int,setplparam) P___((R___(struct plPlotterParamsStruct *_plotter_params) const char *parameter, voidptr_t value));

  /* PUBLIC DATA: user-specified (recognized) Plotter parameters */
  voidptr_t plparams[NUM_PLOTTER_PARAMETERS];
}
#ifdef NOT_LIBPLOTTER
PlotterParams;
#else  /* not NOT_LIBPLOTTER */
;
#endif /* not NOT_LIBPLOTTER */

/* The Plotter class (or struct) definition.  There are many members! */

#ifndef NOT_LIBPLOTTER
class Plotter
#else
typedef struct plPlotterStruct	/* this tag is used only by libplot */
#endif
{
#ifndef NOT_LIBPLOTTER
 private:
  /* disallow copying and assignment */
  Plotter (const Plotter& oldplotter);  
  Plotter& operator= (const Plotter& oldplotter);

  /* Private functions related to the drawing of text strings in Hershey
     fonts (all defined in g_alab_her.c).  In libplot they're declared in
     libplot/extern.h.  */
  double _alabel_hershey (const unsigned char *s, int x_justify, int y_justify);
  double _flabelwidth_hershey (const unsigned char *s);
  void _draw_hershey_glyph (int num, double charsize, int type, bool oblique);
  void _draw_hershey_penup_stroke (double dx, double dy, double charsize, bool oblique);
  void _draw_hershey_string (const unsigned short *string);
  void _draw_hershey_stroke (bool pendown, double deltax, double deltay);

  /* Other private functions (a mixed bag).  In libplot they're declared
     in libplot/extern.h. */
  double _render_non_hershey_string (const char *s, bool do_render, int x_justify, int y_justify);
  double _render_simple_string (const unsigned char *s, bool do_render, int h_just, int v_just);
  unsigned short * _controlify (const unsigned char *);
  void _copy_params_to_plotter (const PlotterParams *params);
  void _create_first_drawing_state (void);
  void _delete_first_drawing_state (void);
  void _free_params_in_plotter (void);
  void _maybe_replace_arc (void);
  void _set_font (void);

 public:
  /* PLOTTER CTORS (old-style, not thread-safe) */
  Plotter (FILE *infile, FILE *outfile, FILE *errfile);
  Plotter (FILE *outfile);
  Plotter (istream& in, ostream& out, ostream& err);
  Plotter (ostream& out);
  Plotter ();
  /* PLOTTER CTORS (new-style, thread-safe) */
  Plotter (FILE *infile, FILE *outfile, FILE *errfile, PlotterParams &params);
  Plotter (FILE *outfile, PlotterParams &params);
  Plotter (istream& in, ostream& out, ostream& err, PlotterParams &params);
  Plotter (ostream& out, PlotterParams &params);
  Plotter (PlotterParams &params);
  /* PLOTTER DTOR */
  virtual ~Plotter ();

  /* PLOTTER PUBLIC METHOD (static, used by old [non-thread-safe] bindings) */
  static int parampl (const char *parameter, voidptr_t value);

  /* PLOTTER PUBLIC METHODS.

     The methods in the libplot/libplotter API.  The QQ___() and other
     macros fix things so that in libplotter, these are declared as
     non-virtual Plotter class methods.  The macros are now unnecessary,
     because in libplot, the methods are declared not here, but in
     extern.h.

     Note that in the code, these Plotter methods appear under the names
     _API_alabel() etc.  Via #define's in extern.h, they're renamed as
     _pl_alable_r() etc. in libplot, and as Plotter::alabel etc. in
     libplotter. */

  QQ___(int,alabel) P___((R___(struct plPlotterStruct *_plotter) int x_justify, int y_justify, const char *s));
  QQ___(int,arc) P___((R___(struct plPlotterStruct *_plotter) int xc, int yc, int x0, int y0, int x1, int y1));
  QQ___(int,arcrel) P___((R___(struct plPlotterStruct *_plotter) int dxc, int dyc, int dx0, int dy0, int dx1, int dy1));
  QQ___(int,bezier2) P___((R___(struct plPlotterStruct *_plotter) int x0, int y0, int x1, int y1, int x2, int y2));
  QQ___(int,bezier2rel) P___((R___(struct plPlotterStruct *_plotter) int dx0, int dy0, int dx1, int dy1, int dx2, int dy2));
  QQ___(int,bezier3) P___((R___(struct plPlotterStruct *_plotter) int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3));
  QQ___(int,bezier3rel) P___((R___(struct plPlotterStruct *_plotter) int dx0, int dy0, int dx1, int dy1, int dx2, int dy2, int dx3, int dy3));
  QQ___(int,bgcolor) P___((R___(struct plPlotterStruct *_plotter) int red, int green, int blue));
  QQ___(int,bgcolorname) P___((R___(struct plPlotterStruct *_plotter) const char *name));
  QQ___(int,box) P___((R___(struct plPlotterStruct *_plotter) int x0, int y0, int x1, int y1));
  QQ___(int,boxrel) P___((R___(struct plPlotterStruct *_plotter) int dx0, int dy0, int dx1, int dy1));
  QQ___(int,capmod) P___((R___(struct plPlotterStruct *_plotter) const char *s));
  QQ___(int,circle) P___((R___(struct plPlotterStruct *_plotter) int x, int y, int r));
  QQ___(int,circlerel) P___((R___(struct plPlotterStruct *_plotter) int dx, int dy, int r));
  QQ___(int,closepath) P___((S___(struct plPlotterStruct *_plotter)));
  QQ___(int,closepl) P___((S___(struct plPlotterStruct *_plotter)));
  QQ___(int,color) P___((R___(struct plPlotterStruct *_plotter) int red, int green, int blue));
  QQ___(int,colorname) P___((R___(struct plPlotterStruct *_plotter) const char *name));
  QQ___(int,cont) P___((R___(struct plPlotterStruct *_plotter) int x, int y));
  QQ___(int,contrel) P___((R___(struct plPlotterStruct *_plotter) int dx, int dy));
  QQ___(int,ellarc) P___((R___(struct plPlotterStruct *_plotter) int xc, int yc, int x0, int y0, int x1, int y1));
  QQ___(int,ellarcrel) P___((R___(struct plPlotterStruct *_plotter) int dxc, int dyc, int dx0, int dy0, int dx1, int dy1));
  QQ___(int,ellipse) P___((R___(struct plPlotterStruct *_plotter) int x, int y, int rx, int ry, int angle));
  QQ___(int,ellipserel) P___((R___(struct plPlotterStruct *_plotter) int dx, int dy, int rx, int ry, int angle));
  QQ___(int,endpath) P___((S___(struct plPlotterStruct *_plotter)));
  QQ___(int,endsubpath) P___((S___(struct plPlotterStruct *_plotter)));
  QQ___(int,erase) P___((S___(struct plPlotterStruct *_plotter)));
  QQ___(int,farc) P___((R___(struct plPlotterStruct *_plotter) double xc, double yc, double x0, double y0, double x1, double y1));
  QQ___(int,farcrel) P___((R___(struct plPlotterStruct *_plotter) double dxc, double dyc, double dx0, double dy0, double dx1, double dy1));
  QQ___(int,fbezier2) P___((R___(struct plPlotterStruct *_plotter) double x0, double y0, double x1, double y1, double x2, double y2));
  QQ___(int,fbezier2rel) P___((R___(struct plPlotterStruct *_plotter) double dx0, double dy0, double dx1, double dy1, double dx2, double dy2));
  QQ___(int,fbezier3) P___((R___(struct plPlotterStruct *_plotter) double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3));
  QQ___(int,fbezier3rel) P___((R___(struct plPlotterStruct *_plotter) double dx0, double dy0, double dx1, double dy1, double dx2, double dy2, double dx3, double dy3));
  QQ___(int,fbox) P___((R___(struct plPlotterStruct *_plotter) double x0, double y0, double x1, double y1));
  QQ___(int,fboxrel) P___((R___(struct plPlotterStruct *_plotter) double dx0, double dy0, double dx1, double dy1));
  QQ___(int,fcircle) P___((R___(struct plPlotterStruct *_plotter) double x, double y, double r));
  QQ___(int,fcirclerel) P___((R___(struct plPlotterStruct *_plotter) double dx, double dy, double r));
  QQ___(int,fconcat) P___((R___(struct plPlotterStruct *_plotter) double m0, double m1, double m2, double m3, double m4, double m5));
  QQ___(int,fcont) P___((R___(struct plPlotterStruct *_plotter) double x, double y));
  QQ___(int,fcontrel) P___((R___(struct plPlotterStruct *_plotter) double dx, double dy));
  QQ___(int,fellarc) P___((R___(struct plPlotterStruct *_plotter) double xc, double yc, double x0, double y0, double x1, double y1));
  QQ___(int,fellarcrel) P___((R___(struct plPlotterStruct *_plotter) double dxc, double dyc, double dx0, double dy0, double dx1, double dy1));
  QQ___(int,fellipse) P___((R___(struct plPlotterStruct *_plotter) double x, double y, double rx, double ry, double angle));
  QQ___(int,fellipserel) P___((R___(struct plPlotterStruct *_plotter) double dx, double dy, double rx, double ry, double angle));
  QQ___(double,ffontname) P___((R___(struct plPlotterStruct *_plotter) const char *s));
  QQ___(double,ffontsize) P___((R___(struct plPlotterStruct *_plotter) double size));
  QQ___(int,fillcolor) P___((R___(struct plPlotterStruct *_plotter) int red, int green, int blue));
  QQ___(int,fillcolorname) P___((R___(struct plPlotterStruct *_plotter) const char *name));
  QQ___(int,fillmod) P___((R___(struct plPlotterStruct *_plotter) const char *s));
  QQ___(int,filltype) P___((R___(struct plPlotterStruct *_plotter) int level));
  QQ___(double,flabelwidth) P___((R___(struct plPlotterStruct *_plotter) const char *s));
  QQ___(int,fline) P___((R___(struct plPlotterStruct *_plotter) double x0, double y0, double x1, double y1));
  QQ___(int,flinedash) P___((R___(struct plPlotterStruct *_plotter) int n, const double *dashes, double offset));
  QQ___(int,flinerel) P___((R___(struct plPlotterStruct *_plotter) double dx0, double dy0, double dx1, double dy1));
  QQ___(int,flinewidth) P___((R___(struct plPlotterStruct *_plotter) double size));
  QQ___(int,flushpl) P___((S___(struct plPlotterStruct *_plotter)));
  QQ___(int,fmarker) P___((R___(struct plPlotterStruct *_plotter) double x, double y, int type, double size));
  QQ___(int,fmarkerrel) P___((R___(struct plPlotterStruct *_plotter) double dx, double dy, int type, double size));
  QQ___(int,fmiterlimit) P___((R___(struct plPlotterStruct *_plotter) double limit));
  QQ___(int,fmove) P___((R___(struct plPlotterStruct *_plotter) double x, double y));
  QQ___(int,fmoverel) P___((R___(struct plPlotterStruct *_plotter) double dx, double dy));
  QQ___(int,fontname) P___((R___(struct plPlotterStruct *_plotter) const char *s));
  QQ___(int,fontsize) P___((R___(struct plPlotterStruct *_plotter) int size));
  QQ___(int,fpoint) P___((R___(struct plPlotterStruct *_plotter) double x, double y));
  QQ___(int,fpointrel) P___((R___(struct plPlotterStruct *_plotter) double dx, double dy));
  QQ___(int,frotate) P___((R___(struct plPlotterStruct *_plotter) double theta));
  QQ___(int,fscale) P___((R___(struct plPlotterStruct *_plotter) double x, double y));
  QQ___(int,fsetmatrix) P___((R___(struct plPlotterStruct *_plotter) double m0, double m1, double m2, double m3, double m4, double m5));
  QQ___(int,fspace) P___((R___(struct plPlotterStruct *_plotter) double x0, double y0, double x1, double y1));
  QQ___(int,fspace2) P___((R___(struct plPlotterStruct *_plotter) double x0, double y0, double x1, double y1, double x2, double y2));
  QQ___(double,ftextangle) P___((R___(struct plPlotterStruct *_plotter) double angle));
  QQ___(int,ftranslate) P___((R___(struct plPlotterStruct *_plotter) double x, double y));
  QQ___(int,havecap) P___((R___(struct plPlotterStruct *_plotter) const char *s));
  QQ___(int,joinmod) P___((R___(struct plPlotterStruct *_plotter) const char *s));
  QQ___(int,label) P___((R___(struct plPlotterStruct *_plotter) const char *s));
  QQ___(int,labelwidth) P___((R___(struct plPlotterStruct *_plotter) const char *s));
  QQ___(int,line) P___((R___(struct plPlotterStruct *_plotter) int x0, int y0, int x1, int y1));
  QQ___(int,linedash) P___((R___(struct plPlotterStruct *_plotter) int n, const int *dashes, int offset));
  QQ___(int,linemod) P___((R___(struct plPlotterStruct *_plotter) const char *s));
  QQ___(int,linerel) P___((R___(struct plPlotterStruct *_plotter) int dx0, int dy0, int dx1, int dy1));
  QQ___(int,linewidth) P___((R___(struct plPlotterStruct *_plotter) int size));
  QQ___(int,marker) P___((R___(struct plPlotterStruct *_plotter) int x, int y, int type, int size));
  QQ___(int,markerrel) P___((R___(struct plPlotterStruct *_plotter) int dx, int dy, int type, int size));
  QQ___(int,move) P___((R___(struct plPlotterStruct *_plotter) int x, int y));
  QQ___(int,moverel) P___((R___(struct plPlotterStruct *_plotter) int dx, int dy));
  QQ___(int,openpl) P___((S___(struct plPlotterStruct *_plotter)));
  QQ___(int,orientation) P___((R___(struct plPlotterStruct *_plotter) int direction));
  QQ___(FILE*,outfile) P___((R___(struct plPlotterStruct *_plotter) FILE* newstream)); /* OBSOLESCENT */
  QQ___(int,pencolor) P___((R___(struct plPlotterStruct *_plotter) int red, int green, int blue));
  QQ___(int,pencolorname) P___((R___(struct plPlotterStruct *_plotter) const char *name));
  QQ___(int,pentype) P___((R___(struct plPlotterStruct *_plotter) int level));
  QQ___(int,point) P___((R___(struct plPlotterStruct *_plotter) int x, int y));
  QQ___(int,pointrel) P___((R___(struct plPlotterStruct *_plotter) int dx, int dy));
  QQ___(int,restorestate) P___((S___(struct plPlotterStruct *_plotter)));
  QQ___(int,savestate) P___((S___(struct plPlotterStruct *_plotter)));
  QQ___(int,space) P___((R___(struct plPlotterStruct *_plotter) int x0, int y0, int x1, int y1));
  QQ___(int,space2) P___((R___(struct plPlotterStruct *_plotter) int x0, int y0, int x1, int y1, int x2, int y2));
  QQ___(int,textangle) P___((R___(struct plPlotterStruct *_plotter) int angle));

  /* Undocumented public methods that provide access to the font tables
     within libplot/libplotter.  They're used by the graphics programs in
     the plotutils package, to display lists of font names.  In libplot
     they're declared in libplot/extern.h, rather than here. */
  voidptr_t get_hershey_font_info P___((S___(struct plPlotterStruct *_plotter)));
  voidptr_t get_ps_font_info P___((S___(struct plPlotterStruct *_plotter)));
  voidptr_t get_pcl_font_info P___((S___(struct plPlotterStruct *_plotter)));
  voidptr_t get_stick_font_info P___((S___(struct plPlotterStruct *_plotter)));

 protected:
#endif /* not NOT_LIBPLOTTER */

  /* PLOTTER PROTECTED METHODS.  All virtual, i.e. they're allowed to be
     Plotter-specific; the generic versions of these do nothing and are
     overridden in derived classes, to define what a Plotter does. */

  /* Initialization (after creation) and termination (before deletion).
     These are exceptions to the above rule: they're effectively
     constructors and destructors, so even in the generic Plotter class,
     they do useful work.  In derived classes they should always invoke
     their base counterparts. */
  Q___(void,initialize) P___((S___(struct plPlotterStruct *_plotter)));
  Q___(void,terminate) P___((S___(struct plPlotterStruct *_plotter)));

  /* Internal page-related methods, called by the API methods openpl(),
     erase() and closepl().  `true' return value indicates operation
     performed successfully. */
  Q___(bool,begin_page) P___((S___(struct plPlotterStruct *_plotter)));
  Q___(bool,erase_page) P___((S___(struct plPlotterStruct *_plotter)));
  Q___(bool,end_page) P___((S___(struct plPlotterStruct *_plotter)));

  /* Internal `push state' method, called by the API method savestate().
     This is used by a very few types of Plotter to create or initialize
     Plotter-specific fields in a newly created drawing state.  Most
     Plotters don't override the generic version, which simply copies such
     Plotter-specific fields. */
  Q___(void,push_state) P___((S___(struct plPlotterStruct *_plotter)));

  /* Internal `pop state' method, called by the API method restorestate().
     This is used by a very few types of Plotter to delete or tear down
     Plotter-specific fields in a drawing state about to be destroyed.
     Most Plotters don't override the generic version, which does nothing
     to such fields. */
  Q___(void,pop_state) P___((S___(struct plPlotterStruct *_plotter)));

  /* Internal `paint path' method, called when the API method endpath() is
     invoked, to draw any path that has been built up in a Plotter's
     drawing state.  It should paint a single simple path.  The generic
     version of course does nothing. */
  Q___(void,paint_path) P___((S___(struct plPlotterStruct *_plotter)));

  /* Another internal method, called by endpath() first, if the path to
     paint is compound rather than simple.  If the Plotter can paint the
     compound path, this should return `true'; if `false' is returned,
     endpath() will paint using compound path emulation instead.  The
     generic version does nothing, but returns `true'. */
  Q___(bool,paint_paths) P___((S___(struct plPlotterStruct *_plotter)));

  /* Support for flushing out the path buffer when it gets too long.  In
     any Plotter, this predicate is evaluated after any path element is
     added to the path buffer, provided (1) the path buffer has become
     greater than or equal to the `max_unfilled_path_length' Plotter
     parameter, and (2) the path isn't to be filled.  In most Plotters,
     including generic Plotters, this simply returns `true', to indicate
     that the path should be flushed out by invoking endpath().  But in
     Plotters that plot in real time under some circumstances (see below),
     this normally returns `false' under those circumstances. */
  Q___(bool,path_is_flushable) P___((S___(struct plPlotterStruct *_plotter)));

  /* Support for real-time plotting, if desired.  An internal `prepaint
     segments' method, called not when a path is finished by endpath()
     being called, but rather when any single segment is added to it.  (Or
     when several segments, obtained by polygonalizing a higher-level
     primitive, are added to it.)  Only in Plotters that plot in real time
     (by definition!) is this not a no-op. */
  Q___(void,maybe_prepaint_segments) P___((R___(struct plPlotterStruct *_plotter) int prev_num_segments));

  /* Internal `draw marker' method, called when the API method marker() is
     invoked.  Only a very few types of Plotter use this.  Return value
     should indicate whether the marker was drawn; `false' means that a
     generic drawing routine, which creates a marker from other libplot
     primitives, should be used.  (Yes, some Plotter output formats support
     some types of marker but not others!).  The generic version does nothing,
     but returns `false'. */
  Q___(bool,paint_marker) P___((R___(struct plPlotterStruct *_plotter) int type, double size));

  /* Internal `draw point' method, called when the API method point() is
     invoked.  There's no standard definition of a `point', so Plotters are
     free to implement this as they see fit. */
  Q___(void,paint_point) P___((S___(struct plPlotterStruct *_plotter)));

  /* Internal, Plotter-specific versions of the `alabel' and `flabelwidth'
     methods, which are applied to single-line text strings in a single
     font (no escape sequences, etc.).  The API methods alabel and
     flabelwidth are wrappers around these.

     The argument h_just specifies the justification to be used when
     rendering (JUST_LEFT, JUST_RIGHT, or JUST_CENTER).  If a display
     device provides low-level support for non-default (i.e. non-left)
     justification, the Plotter's have_horizontal_justification flag (see
     below) should be set.  Similarly, v_just specifies vertical
     justification (JUST_TOP, JUST_HALF, JUST_BASE, or JUST_BOTTOM). */

  /* The first of these is special, for use by Metafile Plotters only.  Any
     Metafile Plotter has low-level support for drawing labels that include
     any of our many escape sequences.  (Actually, a Metafile Plotter just
     dumps them to the output stream, unchanged. :-)).  So the Metafile
     Plotter class has a special _m_paint_text_string_with_escapes method.
     In all other Plotters, the paint_text_string_with_escapes method
     should be set to the dummy _g_paint_text_string_with_escapes method,
     which does nothing; it'll never be invoked.  Our code recognizes that
     a Metafile Plotter is special by looking at the Plotter's
     `have_escaped_string_support' capability, which is `1' for a Metafile
     Plotter and `0' for all others. */

  Q___(void,paint_text_string_with_escapes) P___((R___(struct plPlotterStruct *_plotter) const unsigned char *s, int x_justify, int y_justify));

  Q___(double,paint_text_string) P___((R___(struct plPlotterStruct *_plotter) const unsigned char *s, int h_just, int v_just));
  Q___(double,get_text_width) P___((R___(struct plPlotterStruct *_plotter) const unsigned char *s));

  /* Low-level, Plotter-specific `retrieve font' function; called by the
     internal _set_font() function, which in turn is called by the API
     methods alabel() and labelwidth(), and by the API methods
     fontname()/fontsize()/textwidth(), but only because they need to
     return a font size.  retrieve_font() is called only if the
     user-specified font is a non-Hershey font.  If it returns false, a
     default font, e.g., a Hershey font, will be substituted. */
  Q___(bool,retrieve_font) P___((S___(struct plPlotterStruct *_plotter)));

  /* Internal `flush output' method, called by the API method flushpl().
     This is called only if the Plotter does its own output, i.e., does not
     write to an output stream.  I.e., only if the Plotter's `output_model'
     data element is PL_OUTPUT_VIA_CUSTOM_ROUTINES_TO_NON_STREAM.  Return
     value indicates whether flushing worked. */
  Q___(bool,flush_output) P___((S___(struct plPlotterStruct *_plotter)));

  /* error handlers */
  Q___(void,warning) P___((R___(struct plPlotterStruct *_plotter) const char *msg));
  Q___(void,error) P___((R___(struct plPlotterStruct *_plotter) const char *msg));

  /* PLOTTER DATA MEMBERS (not specific to any one device driver).  These
     are protected rather than private, so they can be accessed by derived
     classes. */

  /* Basic data members: many parameters affecting Plotter operation, which
     are set by the initialize() method and not changed thereafter, plus a
     few (e.g., pointers to plOutbuf's for holding graphics code) which may
     change at later times. */
  plPlotterData *data;

  /* drawing state stack (pointer to top) */
  plDrawState *drawstate;

#ifdef NOT_LIBPLOTTER
  /* PLOTTER DATA MEMBERS (device driver-specific). */
  /* In libplot, they appear here, i.e., in the Plotter struct.  But in
     libplotter, they don't appear here, i.e. they don't appear in the base
     Plotter class: they appear, more logically, as private or protected
     data members of the appropriate derived classes (for the definitions
     of which, see further below in this file). */

  /* Some of these are constant over the usable lifetime of the Plotter,
     and are set, at latest, in the first call to begin_page().  They are
     just parameters.  Other data members may change, since they represent
     our knowledge of the display device's internal state.  Each of the
     latter is flagged by "D:" (i.e. "dynamic") in its comment line. */

  /* data members specific to Bitmap Plotters */
  voidptr_t b_arc_cache_data;	/* pointer to cache (used by miPolyArc_r) */
  int b_xn, b_yn;		/* bitmap dimensions */
  voidptr_t b_painted_set;	/* D: libxmi's canvas (a (miPaintedSet *)) */
  voidptr_t b_canvas;		/* D: libxmi's canvas (a (miCanvas *)) */
  /* data members specific to Metafile Plotters */
  /* 0. parameters */
  bool meta_portable_output;	/* portable, not binary output format? */
  /* 1. dynamic attributes, general */
  plPoint meta_pos;		/* graphics cursor position */
  bool meta_position_is_unknown; /* position is unknown? */
  double meta_m_user_to_ndc[6];	/* user->NDC transformation matrix */
  /* 2. dynamic attributes, path-related */
  int meta_fill_rule_type;	/* one of FILL_*, determined by fill rule */
  int meta_line_type;		/* one of L_*, determined by line mode */
  bool meta_points_are_connected; /* if not set, path displayed as points */
  int meta_cap_type;		/* one of CAP_*, determined by cap mode */
  int meta_join_type;		/* one of JOIN_*, determined by join mode */
  double meta_miter_limit;	/* miter limit for line joins */
  double meta_line_width;	/* width of lines in user coordinates */
  bool meta_line_width_is_default; /* line width is default value? */
  const double *meta_dash_array; /* array of dash on/off lengths(nonnegative)*/
  int meta_dash_array_len;	/* length of same */
  double meta_dash_offset;	/* offset distance into dash array (`phase') */
  bool meta_dash_array_in_effect; /* dash array should override line mode? */
  int meta_pen_type;		/* pen type (0 = no pen, 1 = pen) */
  int meta_fill_type;		/* fill type (0 = no fill, 1 = fill, ...) */
  int meta_orientation;	        /* orientation of circles etc.(1=c'clockwise)*/
  /* 3. dynamic attributes, text-related */
  const char *meta_font_name;	/* font name */
  double meta_font_size;	/* font size in user coordinates */
  bool meta_font_size_is_default; /* font size is Plotter default? */
  double meta_text_rotation;	/* degrees counterclockwise, for labels */
  /* 4. dynamic color attributes (fgcolor and fillcolor are path-related;
     fgcolor affects other primitives too) */
  plColor meta_fgcolor;		/* foreground color, i.e., pen color */
  plColor meta_fillcolor_base;	/* fill color */
  plColor meta_bgcolor;		/* background color for graphics display */
  /* data members specific to Tektronix Plotters */
  int tek_display_type;		/* which sort of Tektronix? (one of D_*) */
  int tek_mode;			/* D: one of MODE_* */
  int tek_line_type;		/* D: one of L_* */
  bool tek_mode_is_unknown;	/* D: tek mode unknown? */
  bool tek_line_type_is_unknown; /* D: tek line type unknown? */
  int tek_kermit_fgcolor;	/* D: kermit's foreground color */
  int tek_kermit_bgcolor;	/* D: kermit's background color */
  bool tek_position_is_unknown;	/* D: cursor position is unknown? */
  plIntPoint tek_pos;		/* D: Tektronix cursor position */
  /* data members specific to ReGIS Plotters */
  plIntPoint regis_pos;		/* D: ReGIS graphics cursor position */
  bool regis_position_is_unknown; /* D: graphics cursor position is unknown? */
  int regis_line_type;		/* D: native ReGIS line type */
  bool regis_line_type_is_unknown; /* D: ReGIS line type is unknown? */
  int regis_fgcolor;		/* D: ReGIS foreground color, in range 0..7 */
  int regis_bgcolor;		/* D: ReGIS background color, in range 0..7 */
  bool regis_fgcolor_is_unknown; /* D: foreground color unknown? */
  bool regis_bgcolor_is_unknown; /* D: background color unknown? */
  /* data members specific to HP-GL (and PCL) Plotters */
  int hpgl_version;		/* version: 0=HP-GL, 1=HP7550A, 2=HP-GL/2 */
  int hpgl_rotation;		/* rotation angle (0, 90, 180, or 270) */
  double hpgl_plot_length;	/* plot length (for HP-GL/2 roll plotters) */
  plPoint hpgl_p1;		/* scaling point P1 in native HP-GL coors */
  plPoint hpgl_p2;		/* scaling point P2 in native HP-GL coors */
  bool hpgl_have_screened_vectors; /* can shade pen marks? (HP-GL/2 only) */
  bool hpgl_have_char_fill;	/* can shade char interiors? (HP-GL/2 only) */
  bool hpgl_can_assign_colors;	/* can assign pen colors? (HP-GL/2 only) */
  bool hpgl_use_opaque_mode;	/* pen marks sh'd be opaque? (HP-GL/2 only) */
  plColor hpgl_pen_color[HPGL2_MAX_NUM_PENS]; /* D: color array for pens */
  int hpgl_pen_defined[HPGL2_MAX_NUM_PENS];/*D:0=none,1=soft-defd,2=hard-defd*/
  int hpgl_pen;			/* D: number of currently selected pen */
  int hpgl_free_pen;		/* D: pen to be assigned a color next */
  bool hpgl_bad_pen;		/* D: bad pen (advisory, see h_color.c) */
  bool hpgl_pendown;		/* D: pen down rather than up? */
  double hpgl_pen_width;	/* D: pen width(frac of diag dist betw P1,P2)*/
  int hpgl_line_type;		/* D: line type(HP-GL numbering,solid = -100)*/
  int hpgl_cap_style;		/* D: cap style for lines (HP-GL/2 numbering)*/
  int hpgl_join_style;		/* D: join style for lines(HP-GL/2 numbering)*/
  double hpgl_miter_limit;	/* D: miterlimit for line joins(HP-GL/2 only)*/
  int hpgl_pen_type;		/* D: sv type (e.g. HPGL_PEN_{SOLID|SHADED}) */
  double hpgl_pen_option1;	/* D: used for some screened vector types */
  double hpgl_pen_option2;	/* D: used for some screened vector types */
  int hpgl_fill_type;		/* D: fill type (one of FILL_SOLID_UNI etc.) */
  double hpgl_fill_option1;	/* D: used for some fill types */
  double hpgl_fill_option2;	/* D: used for some fill types */
  int hpgl_char_rendering_type;	/* D: character rendering type (fill/edge) */
  int hpgl_symbol_set;		/* D: encoding, 14=ISO-Latin-1 (HP-GL/2 only)*/
  int hpgl_spacing;		/* D: fontspacing,0=fixed,1=not(HP-GL/2 only)*/
  int hpgl_posture;		/* D: posture,0=uprite,1=italic(HP-GL/2 only)*/
  int hpgl_stroke_weight;	/* D: weight,0=normal,3=bold,..(HP-GL/2only)*/
  int hpgl_pcl_typeface;	/* D: PCL typeface, see g_fontdb.c (HP-GL/2) */
  int hpgl_charset_lower;	/* D: HP lower-half charset no. (pre-HP-GL/2)*/
  int hpgl_charset_upper;	/* D: HP upper-half charset no. (pre-HP-GL/2)*/
  double hpgl_rel_char_height;	/* D: char ht., % of p2y-p1y (HP-GL/2 only) */
  double hpgl_rel_char_width;	/* D: char width, % of p2x-p1x (HP-GL/2 only)*/
  double hpgl_rel_label_rise;	/* D: label rise, % of p2y-p1y (HP-GL/2 only)*/
  double hpgl_rel_label_run;	/* D: label run, % of p2x-p1x (HP-GL/2 only) */
  double hpgl_tan_char_slant;	/* D: tan of character slant (HP-GL/2 only)*/
  bool hpgl_position_is_unknown; /* D: HP-GL[/2] cursor position is unknown? */
  plIntPoint hpgl_pos;		/* D: cursor position (integer HP-GL coors) */
/* data members specific to Fig Plotters */
  int fig_drawing_depth;	/* D: fig's curr value for `depth' attribute */
  int fig_num_usercolors;	/* D: number of colors currently defined */
  long int fig_usercolors[FIG_MAX_NUM_USER_COLORS]; /* D: colors we've def'd */
  bool fig_colormap_warning_issued; /* D: issued warning on colormap filling up*/
/* data members specific to CGM Plotters */
  int cgm_encoding;		/* CGM_ENCODING_{BINARY,CHARACTER,CLEAR_TEXT}*/
  int cgm_max_version;		/* upper bound on CGM version number */
  int cgm_version;		/* D: CGM version for file (1, 2, 3, or 4) */
  int cgm_profile;		/* D: CGM_PROFILE_{WEB,MODEL,NONE} */
  int cgm_need_color;		/* D: non-monochrome? */
  int cgm_page_version;		/* D: CGM version for current page */
  int cgm_page_profile;		/* D: CGM_PROFILE_{WEB,MODEL,NONE} */
  bool cgm_page_need_color;	/* D: current page is non-monochrome? */
  plColor cgm_line_color;	/* D: line pen color (24-bit or 48-bit RGB) */
  plColor cgm_edge_color;	/* D: edge pen color (24-bit or 48-bit RGB) */
  plColor cgm_fillcolor;	/* D: fill color (24-bit or 48-bit RGB) */
  plColor cgm_marker_color;	/* D: marker pen color (24-bit or 48-bit RGB)*/
  plColor cgm_text_color;	/* D: text pen color (24-bit or 48-bit RGB) */
  plColor cgm_bgcolor;		/* D: background color (24-bit or 48-bit RGB)*/
  bool cgm_bgcolor_suppressed;	/* D: background color suppressed? */
  int cgm_line_type;		/* D: one of CGM_L_{SOLID, etc.} */
  double cgm_dash_offset;	/* D: offset into dash array (`phase') */
  int cgm_join_style;		/* D: join style for lines (CGM numbering)*/
  int cgm_cap_style;		/* D: cap style for lines (CGM numbering)*/
  int cgm_dash_cap_style;	/* D: dash cap style for lines(CGM numbering)*/
  int cgm_line_width;		/* D: line width in CGM coordinates */
  int cgm_interior_style;	/* D: one of CGM_INT_STYLE_{EMPTY, etc.} */
  int cgm_edge_type;		/* D: one of CGM_L_{SOLID, etc.} */
  double cgm_edge_dash_offset;	/* D: offset into dash array (`phase') */
  int cgm_edge_join_style;	/* D: join style for edges (CGM numbering)*/
  int cgm_edge_cap_style;	/* D: cap style for edges (CGM numbering)*/
  int cgm_edge_dash_cap_style;	/* D: dash cap style for edges(CGM numbering)*/
  int cgm_edge_width;		/* D: edge width in CGM coordinates */
  bool cgm_edge_is_visible;	/* D: filled regions have edges? */
  double cgm_miter_limit;	/* D: CGM's miter limit */
  int cgm_marker_type;		/* D: one of CGM_M_{DOT, etc.} */
  int cgm_marker_size;		/* D: marker size in CGM coordinates */
  int cgm_char_height;		/* D: character height */
  int cgm_char_base_vector_x;	/* D: character base vector */
  int cgm_char_base_vector_y;
  int cgm_char_up_vector_x;	/* D: character up vector */
  int cgm_char_up_vector_y;
  int cgm_horizontal_text_alignment; /* D: one of CGM_ALIGN_* */
  int cgm_vertical_text_alignment; /* D: one of CGM_ALIGN_* */
  int cgm_font_id;		/* D: PS font in range 0..34 */
  int cgm_charset_lower;	/* D: lower charset (index into defined list)*/
  int cgm_charset_upper;	/* D: upper charset (index into defined list)*/
  int cgm_restricted_text_type;	/* D: one of CGM_RESTRICTED_TEXT_TYPE_* */
/* data members specific to Illustrator Plotters */
  int ai_version;		/* AI version 3 or AI version 5? */
  double ai_pen_cyan;		/* D: pen color (in CMYK space) */
  double ai_pen_magenta;
  double ai_pen_yellow;
  double ai_pen_black;
  double ai_fill_cyan;		/* D: fill color (in CMYK space) */
  double ai_fill_magenta;
  double ai_fill_yellow;
  double ai_fill_black;
  bool ai_cyan_used;		/* D: C, M, Y, K have been used? */
  bool ai_magenta_used;
  bool ai_yellow_used;
  bool ai_black_used;
  int ai_cap_style;		/* D: cap style for lines (PS numbering) */
  int ai_join_style;		/* D: join style for lines (PS numbering) */
  double ai_miter_limit;	/* D: miterlimit for line joins */
  int ai_line_type;		/* D: one of L_* */
  double ai_line_width;		/* D: line width in printer's points */
  int ai_fill_rule_type;	/* D: fill rule (FILL_{ODD|NONZERO}_WINDING) */
/* data members specific to SVG Plotters */
  double s_matrix[6];		/* D: default transformation matrix for page */
  bool s_matrix_is_unknown;	/* D: matrix has not yet been set? */
  bool s_matrix_is_bogus;	/* D: matrix has been set, but is bogus? */
  plColor s_bgcolor;		/* D: background color (RGB) */
  bool s_bgcolor_suppressed;	/* D: background color suppressed? */
/* data members specific to PNM Plotters (derived from Bitmap Plotters) */
  bool n_portable_output;	/* portable, not binary output format? */
#ifdef INCLUDE_PNG_SUPPORT
/* data members specific to PNG Plotters (derived from Bitmap Plotters) */
  bool z_interlace;		/* interlaced PNG? */
  bool z_transparent;		/* transparent PNG? */
  plColor z_transparent_color;	/* if so, transparent color (24-bit RGB) */
#endif /* INCLUDE_PNG_SUPPORT */
/* data members specific to GIF Plotters */
  int i_xn, i_yn;		/* bitmap dimensions */
  int i_num_pixels;		/* total pixels (used by scanner) */
  bool i_animation;		/* animated (multi-image) GIF? */
  int i_iterations;		/* number of times GIF should be looped */
  int i_delay;			/* delay after image, in 1/100 sec units */
  bool i_interlace;		/* interlaced GIF? */
  bool i_transparent;		/* transparent GIF? */
  plColor i_transparent_color;	/* if so, transparent color (24-bit RGB) */
  voidptr_t i_arc_cache_data;	/* pointer to cache (used by miPolyArc_r) */
  int i_transparent_index;	/* D: transparent color index (if any) */
  voidptr_t i_painted_set;	/* D: libxmi's canvas (a (miPaintedSet *)) */
  voidptr_t i_canvas;		/* D: libxmi's canvas (a (miCanvas *)) */
  plColor i_colormap[256];	/* D: frame colormap (containing 24-bit RGBs)*/
  int i_num_color_indices;	/* D: number of color indices allocated */
  bool i_frame_nonempty;	/* D: something drawn in current frame? */
  int i_bit_depth;		/* D: bit depth (ceil(log2(num_indices))) */
  int i_pixels_scanned;		/* D: number that scanner has scanned */
  int i_pass;			/* D: scanner pass (used if interlacing) */
  plIntPoint i_hot;		/* D: scanner hot spot */
  plColor i_global_colormap[256]; /* D: colormap for first frame (stashed) */
  int i_num_global_color_indices;/* D: number of indices in global colormap */
  bool i_header_written;	/* D: GIF header written yet? */
#ifndef X_DISPLAY_MISSING
/* data members specific to X Drawable Plotters and X Plotters */
  Display *x_dpy;		/* X display */
  Visual *x_visual;		/* X visual */
  Drawable x_drawable1;		/* an X drawable (e.g. a pixmap) */
  Drawable x_drawable2;		/* an X drawable (e.g. a window) */
  Drawable x_drawable3;		/* graphics buffer, if double buffering */
  int x_double_buffering;	/* double buffering type (if any) */
  long int x_max_polyline_len;	/* limit on polyline len (X display-specific)*/
  plFontRecord *x_fontlist;	/* D: head of list of retrieved X fonts */
  plColorRecord *x_colorlist;	/* D: head of list of retrieved X color cells*/
  Colormap x_cmap;		/* D: colormap */
  int x_cmap_type;		/* D: colormap type (orig./copied/bad) */
  bool x_colormap_warning_issued; /* D: issued warning on colormap filling up*/
  bool x_bg_color_warning_issued; /* D: issued warning on bg color */
  int x_paint_pixel_count;	/* D: times point() is invoked to set a pixel*/
/* additional data members specific to X Plotters */
  XtAppContext y_app_con;	/* application context */
  Widget y_toplevel;		/* toplevel widget */
  Widget y_canvas;		/* Label widget */
  Drawable y_drawable4;		/* used for server-side double buffering */
  bool y_auto_flush;		/* do an XFlush() after each drawing op? */
  bool y_vanish_on_delete;	/* window(s) disappear on Plotter deletion? */
  pid_t *y_pids;		/* D: list of pids of forked-off processes */
  int y_num_pids;		/* D: number of pids in list */
  int y_event_handler_count;	/* D: times that event handler is invoked */
#endif /* not X_DISPLAY_MISSING */
#endif /* NOT_LIBPLOTTER */

#ifndef NOT_LIBPLOTTER
  /* STATIC DATA MEMBERS, protected, which are defined in g_defplot.c.  (In
     libplot, these variables are globals, rather than static members of
     the Plotter class.  That's arranged by #ifdef's in libplot/extern.h.)  */

  /* These maintain a sparse array of pointers to Plotter instances. */
  static Plotter **_plotters;	/* D: sparse array of Plotter instances */
  static int _plotters_len;	/* D: length of sparse array */

  /* This stores the global Plotter parameters used by the old,
     non-thread-safe C++ binding (the user specifies them with
     Plotter::parampl). */
  static PlotterParams *_old_api_global_plotter_params;
#endif /* not NOT_LIBPLOTTER */

  /* PLOTTER PROTECTED FUNCTIONS.  In libplotter they're declared here, as
     protected members of the base Plotter class.  (In libplot they're
     declared in libplot/extern.h.)  Since they're protected, derived
     classes can access them, i.e. call them.  */

#ifndef NOT_LIBPLOTTER
  void _flush_plotter_outstreams (void);
#endif /* NOT_LIBPLOTTER */

}
#ifdef NOT_LIBPLOTTER
Plotter;
#else  /* not NOT_LIBPLOTTER */
;
#endif /* not NOT_LIBPLOTTER */

#undef P___
#undef Q___


#ifndef NOT_LIBPLOTTER
/****************** DERIVED CLASSES (libplotter only) ********************/

/* The derived Plotter classes extensively override the generic Plotter
   methods; the non-private ones, at least.  Note that in libplot, this
   overriding is accomplished differently: `derived' Plotter structs are
   initialized to contain function pointers that may point to the
   non-generic methods.  The files ?_defplot.c contain the structures
   which, in libplot, are used to initialize the function-pointer part of
   the derived Plotter structs.

   The device-specific data members which, in libplot, all appear in every
   Plotter struct, are in libplotter spread among the derived Plotter
   classes, as they logically should be.  */

/* The MetaPlotter class, which produces GNU metafile output */
class MetaPlotter : public Plotter
{
 private:
  /* disallow copying and assignment */
  MetaPlotter (const MetaPlotter& oldplotter);  
  MetaPlotter& operator= (const MetaPlotter& oldplotter);
 public:
  /* ctors (old-style, not thread-safe) */
  MetaPlotter (FILE *infile, FILE *outfile, FILE *errfile);
  MetaPlotter (FILE *outfile);
  MetaPlotter (istream& in, ostream& out, ostream& err);
  MetaPlotter (ostream& out);
  MetaPlotter ();
  /* ctors (new-style, thread-safe) */
  MetaPlotter (FILE *infile, FILE *outfile, FILE *errfile, PlotterParams &params);
  MetaPlotter (FILE *outfile, PlotterParams &params);
  MetaPlotter (istream& in, ostream& out, ostream& err, PlotterParams &params);
  MetaPlotter (ostream& out, PlotterParams &params);
  MetaPlotter (PlotterParams &params);
  /* dtor */
  virtual ~MetaPlotter ();
 protected:
  /* protected methods (overriding Plotter methods) */
  bool begin_page (void);
  bool end_page (void);
  bool erase_page (void);
  bool paint_marker (int type, double size);
  bool paint_paths (void);
  bool path_is_flushable (void);
  void paint_text_string_with_escapes (const unsigned char *s, int h_just, int v_just);
  void initialize (void);
  void maybe_prepaint_segments (int prev_num_segments);
  void paint_path (void);
  void paint_point (void);
  void terminate (void);
  /* MetaPlotter-specific internal functions */
  void _m_emit_integer (int x);
  void _m_emit_float (double x);  
  void _m_emit_op_code (int c);  
  void _m_emit_string (const char *s);  
  void _m_emit_terminator (void);
  void _m_paint_path_internal (const plPath *path);
  void _m_set_attributes (unsigned int mask);
  /* MetaPlotter-specific data members */
  /* 0. parameters */
  bool meta_portable_output;	/* portable, not binary output format? */
  /* 1. dynamic attributes, general */
  plPoint meta_pos;		/* graphics cursor position */
  bool meta_position_is_unknown; /* position is unknown? */
  double meta_m_user_to_ndc[6];	/* user->NDC transformation matrix */
  /* 2. dynamic attributes, path-related */
  int meta_fill_rule_type;	/* one of FILL_*, determined by fill rule */
  int meta_line_type;		/* one of L_*, determined by line mode */
  bool meta_points_are_connected; /* if not set, path displayed as points */
  int meta_cap_type;		/* one of CAP_*, determined by cap mode */
  int meta_join_type;		/* one of JOIN_*, determined by join mode */
  double meta_miter_limit;	/* miter limit for line joins */
  double meta_line_width;	/* width of lines in user coordinates */
  bool meta_line_width_is_default; /* line width is default value? */
  const double *meta_dash_array; /* array of dash on/off lengths(nonnegative)*/
  int meta_dash_array_len;	/* length of same */
  double meta_dash_offset;	/* offset distance into dash array (`phase') */
  bool meta_dash_array_in_effect; /* dash array should override line mode? */
  int meta_pen_type;		/* pen type (0 = no pen, 1 = pen) */
  int meta_fill_type;		/* fill type (0 = no fill, 1 = fill, ...) */
  int meta_orientation;	        /* orientation of circles etc.(1=c'clockwise)*/
  /* 3. dynamic attributes, text-related */
  const char *meta_font_name;	/* font name */
  double meta_font_size;	/* font size in user coordinates */
  bool meta_font_size_is_default; /* font size is Plotter default? */
  double meta_text_rotation;	/* degrees counterclockwise, for labels */
  /* 4. dynamic color attributes (fgcolor and fillcolor are path-related;
     fgcolor affects other primitives too) */
  plColor meta_fgcolor;		/* foreground color, i.e., pen color */
  plColor meta_fillcolor_base;	/* fill color */
  plColor meta_bgcolor;		/* background color for graphics display */
};

/* The BitmapPlotter class, from which PNMPlotter and PNGPlotter are derived */
class BitmapPlotter : public Plotter
{
 private:
  /* disallow copying and assignment */
  BitmapPlotter (const BitmapPlotter& oldplotter);  
  BitmapPlotter& operator= (const BitmapPlotter& oldplotter);
 public:
  /* ctors (old-style, not thread-safe) */
  BitmapPlotter (FILE *infile, FILE *outfile, FILE *errfile);
  BitmapPlotter (FILE *outfile);
  BitmapPlotter (istream& in, ostream& out, ostream& err);
  BitmapPlotter (ostream& out);
  BitmapPlotter ();
  /* ctors (new-style, thread-safe) */
  BitmapPlotter (FILE *infile, FILE *outfile, FILE *errfile, PlotterParams &params);
  BitmapPlotter (FILE *outfile, PlotterParams &params);
  BitmapPlotter (istream& in, ostream& out, ostream& err, PlotterParams &params);
  BitmapPlotter (ostream& out, PlotterParams &params);
  BitmapPlotter (PlotterParams &params);
  /* dtor */
  virtual ~BitmapPlotter ();
 protected:
  /* protected methods (overriding Plotter methods) */
  bool begin_page (void);
  bool erase_page (void);
  bool end_page (void);
  void paint_point (void);
  void initialize (void);
  void terminate (void);
  void paint_path (void);
  bool paint_paths (void);
  /* internal functions that are overridden in derived classes (crocks) */
  virtual int _maybe_output_image (void);
  /* BitmapPlotter-specific internal functions */
  void _b_delete_image (void);
  void _b_draw_elliptic_arc (plPoint p0, plPoint p1, plPoint pc);
  void _b_draw_elliptic_arc_2 (plPoint p0, plPoint p1, plPoint pc);
  void _b_draw_elliptic_arc_internal (int xorigin, int yorigin, unsigned int squaresize_x, unsigned int squaresize_y, int startangle, int anglerange);
  void _b_new_image (void);
  /* BitmapPlotter-specific data members */
  voidptr_t b_arc_cache_data;	/* pointer to cache (used by miPolyArc_r) */
  int b_xn, b_yn;		/* bitmap dimensions */
  voidptr_t b_painted_set;	/* D: libxmi's canvas (a (miPaintedSet *)) */
  voidptr_t b_canvas;		/* D: libxmi's canvas (a (miCanvas *)) */
};

/* The TekPlotter class, which produces Tektronix output */
class TekPlotter : public Plotter
{
 private:
  /* disallow copying and assignment */
  TekPlotter (const TekPlotter& oldplotter);  
  TekPlotter& operator= (const TekPlotter& oldplotter);
 public:
  /* ctors (old-style, not thread-safe) */
  TekPlotter (FILE *infile, FILE *outfile, FILE *errfile);
  TekPlotter (FILE *outfile);
  TekPlotter (istream& in, ostream& out, ostream& err);
  TekPlotter (ostream& out);
  TekPlotter ();
  /* ctors (new-style, thread-safe) */
  TekPlotter (FILE *infile, FILE *outfile, FILE *errfile, PlotterParams &params);
  TekPlotter (FILE *outfile, PlotterParams &params);
  TekPlotter (istream& in, ostream& out, ostream& err, PlotterParams &params);
  TekPlotter (ostream& out, PlotterParams &params);
  TekPlotter (PlotterParams &params);
  /* dtor */
  virtual ~TekPlotter ();
 protected:
  /* protected methods (overriding Plotter methods) */
  bool begin_page (void);
  bool erase_page (void);
  bool end_page (void);
  bool path_is_flushable (void);
  void paint_point (void);
  void initialize (void);
  void terminate (void);
  void maybe_prepaint_segments (int prev_num_segments);
  /* TekPlotter-specific internal functions */
  void _t_set_attributes (void);
  void _t_set_bg_color (void);
  void _t_set_pen_color (void);
  void _tek_mode (int newmode);
  void _tek_move (int xx, int yy);
  void _tek_vector (int xx, int yy);
  void _tek_vector_compressed (int xx, int yy, int oldxx, int oldyy, bool force);
  /* TekPlotter-specific data members */
  int tek_display_type;		/* which sort of Tektronix? */
  int tek_mode;			/* D: one of MODE_* */
  int tek_line_type;		/* D: one of L_* */
  bool tek_mode_is_unknown;	/* D: tek mode unknown? */
  bool tek_line_type_is_unknown; /* D: tek line type unknown? */
  int tek_kermit_fgcolor;	/* D: kermit's foreground color */
  int tek_kermit_bgcolor;	/* D: kermit's background color */
  bool tek_position_is_unknown;	/* D: cursor position is unknown? */
  plIntPoint tek_pos;		/* D: Tektronix cursor position */
};

/* The ReGISPlotter class, which produces ReGIS output */
class ReGISPlotter : public Plotter
{
 private:
  /* disallow copying and assignment */
  ReGISPlotter (const ReGISPlotter& oldplotter);  
  ReGISPlotter& operator= (const ReGISPlotter& oldplotter);
 public:
  /* ctors (old-style, not thread-safe) */
  ReGISPlotter (FILE *infile, FILE *outfile, FILE *errfile);
  ReGISPlotter (FILE *outfile);
  ReGISPlotter (istream& in, ostream& out, ostream& err);
  ReGISPlotter (ostream& out);
  ReGISPlotter ();
  /* ctors (new-style, thread-safe) */
  ReGISPlotter (FILE *infile, FILE *outfile, FILE *errfile, PlotterParams &params);
  ReGISPlotter (FILE *outfile, PlotterParams &params);
  ReGISPlotter (istream& in, ostream& out, ostream& err, PlotterParams &params);
  ReGISPlotter (ostream& out, PlotterParams &params);
  ReGISPlotter (PlotterParams &params);
  /* dtor */
  virtual ~ReGISPlotter ();
 protected:
  /* protected methods (overriding Plotter methods) */
  bool begin_page (void);
  bool erase_page (void);
  bool end_page (void);
  void paint_point (void);
  bool path_is_flushable (void);
  void initialize (void);
  void terminate (void);
  void maybe_prepaint_segments (int prev_num_segments);
  void paint_path (void);
  bool paint_paths (void);
  /* ReGISPlotter-specific internal functions */
  void _r_set_attributes (void);
  void _r_set_bg_color (void);
  void _r_set_fill_color (void);
  void _r_set_pen_color (void);
  void _regis_move (int xx, int yy);
  /* ReGISPlotter-specific data members */
  plIntPoint regis_pos;		/* D: ReGIS graphics cursor position */
  bool regis_position_is_unknown; /* D: graphics cursor position is unknown? */
  int regis_line_type;		/* D: native ReGIS line type */
  bool regis_line_type_is_unknown; /* D: ReGIS line type is unknown? */
  int regis_fgcolor;		/* D: ReGIS foreground color, in range 0..7 */
  int regis_bgcolor;		/* D: ReGIS background color, in range 0..7 */
  bool regis_fgcolor_is_unknown; /* D: foreground color unknown? */
  bool regis_bgcolor_is_unknown; /* D: background color unknown? */
};

/* The HPGLPlotter class, which produces HP-GL or HP-GL/2 output */
class HPGLPlotter : public Plotter
{
 private:
  /* disallow copying and assignment */
  HPGLPlotter (const HPGLPlotter& oldplotter);  
  HPGLPlotter& operator= (const HPGLPlotter& oldplotter);
 public:
  /* ctors (old-style, not thread-safe) */
  HPGLPlotter (FILE *infile, FILE *outfile, FILE *errfile);
  HPGLPlotter (FILE *outfile);
  HPGLPlotter (istream& in, ostream& out, ostream& err);
  HPGLPlotter (ostream& out);
  HPGLPlotter ();
  /* ctors (new-style, thread-safe) */
  HPGLPlotter (FILE *infile, FILE *outfile, FILE *errfile, PlotterParams &params);
  HPGLPlotter (FILE *outfile, PlotterParams &params);
  HPGLPlotter (istream& in, ostream& out, ostream& err, PlotterParams &params);
  HPGLPlotter (ostream& out, PlotterParams &params);
  HPGLPlotter (PlotterParams &params);
  /* dtor */
  virtual ~HPGLPlotter ();
 protected:
  /* protected methods (overriding Plotter methods, overridden in
     PCLPlotter class */
  void initialize (void);
  void terminate (void);
  /* protected methods (overriding Plotter methods) */
  bool begin_page (void);
  bool erase_page (void);
  bool end_page (void);
  void paint_point (void);
  void paint_path (void);
  bool paint_paths (void);
  double paint_text_string (const unsigned char *s, int h_just, int v_just);
  /* internal functions that are overridden in the PCLPlotter class */
  virtual void _maybe_switch_to_hpgl (void);
  virtual void _maybe_switch_from_hpgl (void);
  /* other HPGLPlotter-specific internal functions */
  bool _hpgl2_maybe_update_font (void);
  bool _hpgl_maybe_update_font (void);
  bool _parse_pen_string (const char *pen_s);
  int _hpgl_pseudocolor (int red, int green, int blue, bool restrict_white);
  void _h_set_attributes (void);
  void _h_set_fill_color (bool force_pen_color);
  void _h_set_font (void);
  void _h_set_pen_color (int hpgl_object_type);
  void _h_set_position (void);
  void _hpgl_shaded_pseudocolor (int red, int green, int blue, int *pen, double *shading);
  void _set_hpgl_fill_type (int fill_type, double option1, double option2);
  void _set_hpgl_pen_type (int pen_type, double option1, double option2);
  void _set_hpgl_pen (int pen);
  /* HPGLPlotter-specific data members */
  int hpgl_version;		/* version: 0=HP-GL, 1=HP7550A, 2=HP-GL/2 */
  int hpgl_rotation;		/* rotation angle (0, 90, 180, or 270) */
  double hpgl_plot_length;	/* plot length (for HP-GL/2 roll plotters) */
  plPoint hpgl_p1;		/* scaling point P1 in native HP-GL coors */
  plPoint hpgl_p2;		/* scaling point P2 in native HP-GL coors */
  bool hpgl_have_screened_vectors; /* can shade pen marks? (HP-GL/2 only) */
  bool hpgl_have_char_fill;	/* can shade char interiors? (HP-GL/2 only) */
  bool hpgl_can_assign_colors;	/* can assign pen colors? (HP-GL/2 only) */
  bool hpgl_use_opaque_mode;	/* pen marks sh'd be opaque? (HP-GL/2 only) */
  plColor hpgl_pen_color[HPGL2_MAX_NUM_PENS]; /* D: color array for pens */
  int hpgl_pen_defined[HPGL2_MAX_NUM_PENS];/*D:0=none,1=soft-defd,2=hard-defd*/
  int hpgl_pen;			/* D: number of currently selected pen */
  int hpgl_free_pen;		/* D: pen to be assigned a color next */
  bool hpgl_bad_pen;		/* D: bad pen (advisory, see h_color.c) */
  bool hpgl_pendown;		/* D: pen down rather than up? */
  double hpgl_pen_width;	/* D: pen width(frac of diag dist betw P1,P2)*/
  int hpgl_line_type;		/* D: line type(HP-GL numbering,solid = -100)*/
  int hpgl_cap_style;		/* D: cap style for lines (HP-GL/2 numbering)*/
  int hpgl_join_style;		/* D: join style for lines(HP-GL/2 numbering)*/
  double hpgl_miter_limit;	/* D: miterlimit for line joins(HP-GL/2 only)*/
  int hpgl_pen_type;		/* D: sv type (e.g. HPGL_PEN_{SOLID|SHADED}) */
  double hpgl_pen_option1;	/* D: used for some screened vector types */
  double hpgl_pen_option2;	/* D: used for some screened vector types */
  int hpgl_fill_type;		/* D: fill type (one of FILL_SOLID_UNI etc.) */
  double hpgl_fill_option1;	/* D: used for some fill types */
  double hpgl_fill_option2;	/* D: used for some fill types */
  int hpgl_char_rendering_type;	/* D: character rendering type (fill/edge) */
  int hpgl_symbol_set;		/* D: encoding, 14=ISO-Latin-1 (HP-GL/2 only)*/
  int hpgl_spacing;		/* D: fontspacing,0=fixed,1=not(HP-GL/2 only)*/
  int hpgl_posture;		/* D: posture,0=uprite,1=italic(HP-GL/2 only)*/
  int hpgl_stroke_weight;	/* D: weight,0=normal,3=bold,..(HP-GL/2only)*/
  int hpgl_pcl_typeface;	/* D: typeface, see g_fontdb.c (HP-GL/2) */
  int hpgl_charset_lower;	/* D: HP lower-half charset no. (pre-HP-GL/2)*/
  int hpgl_charset_upper;	/* D: HP upper-half charset no. (pre-HP-GL/2)*/
  double hpgl_rel_char_height;	/* D: char ht., % of p2y-p1y (HP-GL/2 only) */
  double hpgl_rel_char_width;	/* D: char width, % of p2x-p1x (HP-GL/2 only)*/
  double hpgl_rel_label_rise;	/* D: label rise, % of p2y-p1y (HP-GL/2 only)*/
  double hpgl_rel_label_run;	/* D: label run, % of p2x-p1x (HP-GL/2 only) */
  double hpgl_tan_char_slant;	/* D: tan of character slant (HP-GL/2 only)*/
  bool hpgl_position_is_unknown; /* D: HP-GL[/2] cursor position is unknown? */
  plIntPoint hpgl_pos;		/* D: cursor position (integer HP-GL coors) */
};

/* The PCLPlotter class, which produces PCL 5 output */
class PCLPlotter : public HPGLPlotter
{
 private:
  /* disallow copying and assignment */
  PCLPlotter (const PCLPlotter& oldplotter);  
  PCLPlotter& operator= (const PCLPlotter& oldplotter);
 public:
  /* ctors (old-style, not thread-safe) */
  PCLPlotter (FILE *infile, FILE *outfile, FILE *errfile);
  PCLPlotter (FILE *outfile);
  PCLPlotter (istream& in, ostream& out, ostream& err);
  PCLPlotter (ostream& out);
  PCLPlotter ();
  /* ctors (new-style, thread-safe) */
  PCLPlotter (FILE *infile, FILE *outfile, FILE *errfile, PlotterParams &params);
  PCLPlotter (FILE *outfile, PlotterParams &params);
  PCLPlotter (istream& in, ostream& out, ostream& err, PlotterParams &params);
  PCLPlotter (ostream& out, PlotterParams &params);
  PCLPlotter (PlotterParams &params);
  /* dtor */
  virtual ~PCLPlotter ();
 protected:
  /* protected methods (overriding HPGLPlotter methods) */
  void initialize (void);
  void terminate (void);
  /* internal functions that override HPGLPlotter internal functions */
  void _maybe_switch_to_hpgl (void);
  void _maybe_switch_from_hpgl (void);
};

/* The FigPlotter class, which produces Fig-format output for xfig */
class FigPlotter : public Plotter
{
 private:
  /* disallow copying and assignment */
  FigPlotter (const FigPlotter& oldplotter);  
  FigPlotter& operator= (const FigPlotter& oldplotter);
 public:
  /* ctors (old-style, not thread-safe) */
  FigPlotter (FILE *infile, FILE *outfile, FILE *errfile);
  FigPlotter (FILE *outfile);
  FigPlotter (istream& in, ostream& out, ostream& err);
  FigPlotter (ostream& out);
  FigPlotter ();
  /* ctors (new-style, thread-safe) */
  FigPlotter (FILE *infile, FILE *outfile, FILE *errfile, PlotterParams &params);
  FigPlotter (FILE *outfile, PlotterParams &params);
  FigPlotter (istream& in, ostream& out, ostream& err, PlotterParams &params);
  FigPlotter (ostream& out, PlotterParams &params);
  FigPlotter (PlotterParams &params);
  /* dtor */
  virtual ~FigPlotter ();
 protected:
  /* protected methods (overriding Plotter methods) */
  bool begin_page (void);
  bool erase_page (void);
  bool end_page (void);
  void paint_point (void);
  void initialize (void);
  void terminate (void);
  void paint_path (void);
  bool paint_paths (void);
  double paint_text_string (const unsigned char *s, int h_just, int v_just);
  bool retrieve_font (void);
  /* FigPlotter-specific internal functions */
  int _fig_color (int red, int green, int blue);
  void _f_compute_line_style (int *style, double *spacing);
  void _f_draw_arc_internal (double xc, double yc, double x0, double y0, double x1, double y1);
  void _f_draw_box_internal (plPoint p0, plPoint p1);
  void _f_draw_ellipse_internal (double x, double y, double rx, double ry, double angle, int subtype);
  void _f_set_fill_color (void);
  void _f_set_pen_color (void);
  /* FigPlotter-specific data members */
  int fig_drawing_depth;	/* D: fig's curr value for `depth' attribute */
  int fig_num_usercolors;	/* D: number of colors currently defined */
  long int fig_usercolors[FIG_MAX_NUM_USER_COLORS]; /* D: colors we've def'd */
  bool fig_colormap_warning_issued; /* D: issued warning on colormap filling up*/
};

/* The CGMPlotter class, which produces CGM (Computer Graphics Metafile)
   output */
class CGMPlotter : public Plotter
{
 private:
  /* disallow copying and assignment */
  CGMPlotter (const CGMPlotter& oldplotter);  
  CGMPlotter& operator= (const CGMPlotter& oldplotter);
 public:
  /* ctors (old-style, not thread-safe) */
  CGMPlotter (FILE *infile, FILE *outfile, FILE *errfile);
  CGMPlotter (FILE *outfile);
  CGMPlotter (istream& in, ostream& out, ostream& err);
  CGMPlotter (ostream& out);
  CGMPlotter ();
  /* ctors (new-style, thread-safe) */
  CGMPlotter (FILE *infile, FILE *outfile, FILE *errfile, PlotterParams &params);
  CGMPlotter (FILE *outfile, PlotterParams &params);
  CGMPlotter (istream& in, ostream& out, ostream& err, PlotterParams &params);
  CGMPlotter (ostream& out, PlotterParams &params);
  CGMPlotter (PlotterParams &params);
  /* dtor */
  virtual ~CGMPlotter ();
 protected:
  /* protected methods (overriding Plotter methods) */
  bool begin_page (void);
  bool erase_page (void);
  bool end_page (void);
  void paint_point (void);
  void initialize (void);
  void terminate (void);
  void paint_path (void);
  bool paint_marker (int type, double size);
  bool paint_paths (void);
  double paint_text_string (const unsigned char *s, int h_just, int v_just);
  /* internal functions */
  void _c_set_attributes (int cgm_object_type);
  void _c_set_bg_color (void);
  void _c_set_fill_color (int cgm_object_type);
  void _c_set_pen_color (int cgm_object_type);
  /* CGMPlotter-specific data members */
  int cgm_encoding;		/* CGM_ENCODING_{BINARY,CHARACTER,CLEAR_TEXT}*/
  int cgm_max_version;		/* upper bound on CGM version number */
  int cgm_version;		/* D: CGM version for file (1, 2, 3, or 4) */
  int cgm_profile;		/* D: CGM_PROFILE_{WEB,MODEL,NONE} */
  int cgm_need_color;		/* D: non-monochrome? */
  int cgm_page_version;		/* D: CGM version for current page */
  int cgm_page_profile;		/* D: CGM_PROFILE_{WEB,MODEL,NONE} */
  bool cgm_page_need_color;	/* D: current page is non-monochrome? */
  plColor cgm_line_color;	/* D: line pen color (24-bit or 48-bit RGB) */
  plColor cgm_edge_color;	/* D: edge pen color (24-bit or 48-bit RGB) */
  plColor cgm_fillcolor;	/* D: fill color (24-bit or 48-bit RGB) */
  plColor cgm_marker_color;	/* D: marker pen color (24-bit or 48-bit RGB)*/
  plColor cgm_text_color;	/* D: text pen color (24-bit or 48-bit RGB) */
  plColor cgm_bgcolor;		/* D: background color (24-bit or 48-bit RGB)*/
  bool cgm_bgcolor_suppressed;	/* D: background color suppressed? */
  int cgm_line_type;		/* D: one of CGM_L_{SOLID, etc.} */
  double cgm_dash_offset;	/* D: offset into dash array (`phase') */
  int cgm_join_style;		/* D: join style for lines (CGM numbering)*/
  int cgm_cap_style;		/* D: cap style for lines (CGM numbering)*/
  int cgm_dash_cap_style;	/* D: dash cap style for lines(CGM numbering)*/
  int cgm_line_width;		/* D: line width in CGM coordinates */
  int cgm_interior_style;	/* D: one of CGM_INT_STYLE_{EMPTY, etc.} */
  int cgm_edge_type;		/* D: one of CGM_L_{SOLID, etc.} */
  double cgm_edge_dash_offset;	/* D: offset into dash array (`phase') */
  int cgm_edge_join_style;	/* D: join style for edges (CGM numbering)*/
  int cgm_edge_cap_style;	/* D: cap style for edges (CGM numbering)*/
  int cgm_edge_dash_cap_style;	/* D: dash cap style for edges(CGM numbering)*/
  int cgm_edge_width;		/* D: edge width in CGM coordinates */
  bool cgm_edge_is_visible;	/* D: filled regions have edges? */
  double cgm_miter_limit;	/* D: CGM's miter limit */
  int cgm_marker_type;		/* D: one of CGM_M_{DOT, etc.} */
  int cgm_marker_size;		/* D: marker size in CGM coordinates */
  int cgm_char_height;		/* D: character height */
  int cgm_char_base_vector_x;	/* D: character base vector */
  int cgm_char_base_vector_y;
  int cgm_char_up_vector_x;	/* D: character up vector */
  int cgm_char_up_vector_y;
  int cgm_horizontal_text_alignment; /* D: one of CGM_ALIGN_* */
  int cgm_vertical_text_alignment; /* D: one of CGM_ALIGN_* */
  int cgm_font_id;		/* D: PS font in range 0..34 */
  int cgm_charset_lower;	/* D: lower charset (index into defined list)*/
  int cgm_charset_upper;	/* D: upper charset (index into defined list)*/
  int cgm_restricted_text_type;	/* D: one of CGM_RESTRICTED_TEXT_TYPE_* */
};

/* The PSPlotter class, which produces idraw-editable PS output */
class PSPlotter : public Plotter
{
 private:
  /* disallow copying and assignment */
  PSPlotter (const PSPlotter& oldplotter);  
  PSPlotter& operator= (const PSPlotter& oldplotter);
 public:
  /* ctors (old-style, not thread-safe) */
  PSPlotter (FILE *infile, FILE *outfile, FILE *errfile);
  PSPlotter (FILE *outfile);
  PSPlotter (istream& in, ostream& out, ostream& err);
  PSPlotter (ostream& out);
  PSPlotter ();
  /* ctors (new-style, thread-safe) */
  PSPlotter (FILE *infile, FILE *outfile, FILE *errfile, PlotterParams &params);
  PSPlotter (FILE *outfile, PlotterParams &params);
  PSPlotter (istream& in, ostream& out, ostream& err, PlotterParams &params);
  PSPlotter (ostream& out, PlotterParams &params);
  PSPlotter (PlotterParams &params);
  /* dtor */
  virtual ~PSPlotter ();
 protected:
  /* protected methods (overriding Plotter methods) */
  bool begin_page (void);
  bool erase_page (void);
  bool end_page (void);
  void paint_point (void);
  void initialize (void);
  void terminate (void);
  void paint_path (void);
  bool paint_paths (void);
  double paint_text_string (const unsigned char *s, int h_just, int v_just);
  /* PSPlotter-specific internal functions */
  double _p_emit_common_attributes (void);
  void _p_compute_idraw_bgcolor (void);
  void _p_fellipse_internal (double x, double y, double rx, double ry, double angle, bool circlep);
  void _p_set_fill_color (void);
  void _p_set_pen_color (void);
};

/* The AIPlotter class, which produces output editable by Adobe Illustrator */
class AIPlotter : public Plotter
{
 private:
  /* disallow copying and assignment */
  AIPlotter (const AIPlotter& oldplotter);  
  AIPlotter& operator= (const AIPlotter& oldplotter);
 public:
  /* ctors (old-style, not thread-safe) */
  AIPlotter (FILE *infile, FILE *outfile, FILE *errfile);
  AIPlotter (FILE *outfile);
  AIPlotter (istream& in, ostream& out, ostream& err);
  AIPlotter (ostream& out);
  AIPlotter ();
  /* ctors (new-style, thread-safe) */
  AIPlotter (FILE *infile, FILE *outfile, FILE *errfile, PlotterParams &params);
  AIPlotter (FILE *outfile, PlotterParams &params);
  AIPlotter (istream& in, ostream& out, ostream& err, PlotterParams &params);
  AIPlotter (ostream& out, PlotterParams &params);
  AIPlotter (PlotterParams &params);
  /* dtor */
  virtual ~AIPlotter ();
 protected:
  /* protected methods (overriding Plotter methods) */
  bool begin_page (void);
  bool erase_page (void);
  bool end_page (void);
  void paint_point (void);
  void initialize (void);
  void terminate (void);
  void paint_path (void);
  bool paint_paths (void);
  double paint_text_string (const unsigned char *s, int h_just, int v_just);
  /* internal functions */
  void _a_set_attributes (void);
  void _a_set_fill_color (bool force_pen_color);
  void _a_set_pen_color (void);
  /* AIPlotter-specific data members */
  int ai_version;		/* AI3 or AI5? */
  double ai_pen_cyan;		/* D: pen color (in CMYK space) */
  double ai_pen_magenta;
  double ai_pen_yellow;
  double ai_pen_black;
  double ai_fill_cyan;		/* D: fill color (in CMYK space) */
  double ai_fill_magenta;
  double ai_fill_yellow;
  double ai_fill_black;
  bool ai_cyan_used;		/* D: C, M, Y, K have been used? */
  bool ai_magenta_used;
  bool ai_yellow_used;
  bool ai_black_used;
  int ai_cap_style;		/* D: cap style for lines (PS numbering)*/
  int ai_join_style;		/* D: join style for lines(PS numbering)*/
  double ai_miter_limit;	/* D: miterlimit for line joins */
  int ai_line_type;		/* D: one of L_* */
  double ai_line_width;		/* D: line width in printer's points */
  int ai_fill_rule_type;	/* D: fill rule (FILL_{ODD|NONZERO}_WINDING) */
};

/* The SVGPlotter class, which produces SVG output for the Web */
class SVGPlotter : public Plotter
{
 private:
  /* disallow copying and assignment */
  SVGPlotter (const SVGPlotter& oldplotter);  
  SVGPlotter& operator= (const SVGPlotter& oldplotter);
 public:
  /* ctors (old-style, not thread-safe) */
  SVGPlotter (FILE *infile, FILE *outfile, FILE *errfile);
  SVGPlotter (FILE *outfile);
  SVGPlotter (istream& in, ostream& out, ostream& err);
  SVGPlotter (ostream& out);
  SVGPlotter ();
  /* ctors (new-style, thread-safe) */
  SVGPlotter (FILE *infile, FILE *outfile, FILE *errfile, PlotterParams &params);
  SVGPlotter (FILE *outfile, PlotterParams &params);
  SVGPlotter (istream& in, ostream& out, ostream& err, PlotterParams &params);
  SVGPlotter (ostream& out, PlotterParams &params);
  SVGPlotter (PlotterParams &params);
  /* dtor */
  virtual ~SVGPlotter ();
 protected:
  /* protected methods (overriding Plotter methods) */
  bool begin_page (void);
  bool erase_page (void);
  bool end_page (void);
  void paint_point (void);
  void initialize (void);
  void terminate (void);
  void paint_path (void);
  bool paint_paths (void);
  double paint_text_string (const unsigned char *s, int h_just, int v_just);
  /* SVGPlotter-specific internal functions */
  void _s_set_matrix (const double m_base[6], const double m_local[6]);
  /* SVGPlotter-specific data members */
  double s_matrix[6];		/* D: default transformation matrix for page */
  bool s_matrix_is_unknown;	/* D: matrix has not yet been set? */
  bool s_matrix_is_bogus;	/* D: matrix has been set, but is bogus? */
  plColor s_bgcolor;		/* D: background color (RGB) */
  bool s_bgcolor_suppressed;	/* D: background color suppressed? */
};

/* The PNMPlotter class, which produces PBM/PGM/PPM output; derived from
   the BitmapPlotter class */
class PNMPlotter : public BitmapPlotter
{
 private:
  /* disallow copying and assignment */
  PNMPlotter (const PNMPlotter& oldplotter);  
  PNMPlotter& operator= (const PNMPlotter& oldplotter);
 public:
  /* ctors (old-style, not thread-safe) */
  PNMPlotter (FILE *infile, FILE *outfile, FILE *errfile);
  PNMPlotter (FILE *outfile);
  PNMPlotter (istream& in, ostream& out, ostream& err);
  PNMPlotter (ostream& out);
  PNMPlotter ();
  /* ctors (new-style, thread-safe) */
  PNMPlotter (FILE *infile, FILE *outfile, FILE *errfile, PlotterParams &params);
  PNMPlotter (FILE *outfile, PlotterParams &params);
  PNMPlotter (istream& in, ostream& out, ostream& err, PlotterParams &params);
  PNMPlotter (ostream& out, PlotterParams &params);
  PNMPlotter (PlotterParams &params);
  /* dtor */
  virtual ~PNMPlotter ();
 protected:
  /* protected methods (overriding BitmapPlotter methods) */
  void initialize (void);
  void terminate (void);
  /* internal functions that override BitmapPlotter functions (crocks) */
  int _maybe_output_image (void);
  /* other PNMPlotter-specific internal functions */
  void _n_write_pnm (void);
  void _n_write_pbm (void);
  void _n_write_pgm (void);
  void _n_write_ppm (void);
  /* PNMPlotter-specific data members */
  bool n_portable_output;	/* portable, not binary output format? */
};

#ifdef INCLUDE_PNG_SUPPORT
/* The PNGPlotter class, which produces PNG output; derived from the
   BitmapPlotter class */
class PNGPlotter : public BitmapPlotter
{
 private:
  /* disallow copying and assignment */
  PNGPlotter (const PNGPlotter& oldplotter);  
  PNGPlotter& operator= (const PNGPlotter& oldplotter);
 public:
  /* ctors (old-style, not thread-safe) */
  PNGPlotter (FILE *infile, FILE *outfile, FILE *errfile);
  PNGPlotter (FILE *outfile);
  PNGPlotter (istream& in, ostream& out, ostream& err);
  PNGPlotter (ostream& out);
  PNGPlotter ();
  /* ctors (new-style, thread-safe) */
  PNGPlotter (FILE *infile, FILE *outfile, FILE *errfile, PlotterParams &params);
  PNGPlotter (FILE *outfile, PlotterParams &params);
  PNGPlotter (istream& in, ostream& out, ostream& err, PlotterParams &params);
  PNGPlotter (ostream& out, PlotterParams &params);
  PNGPlotter (PlotterParams &params);
  /* dtor */
  virtual ~PNGPlotter ();
 protected:
  /* protected methods (overriding BitmapPlotter methods) */
  void initialize (void);
  void terminate (void);
  /* internal functions that override BitmapPlotter functions (crocks) */
  int _maybe_output_image (void);
  /* PNGPlotter-specific data members */
  bool z_interlace;		/* interlaced PNG? */
  bool z_transparent;		/* transparent PNG? */
  plColor z_transparent_color;	/* if so, transparent color (24-bit RGB) */
};
#endif /* INCLUDE_PNG_SUPPORT */

/* The GIFPlotter class, which produces pseudo-GIF output */
class GIFPlotter : public Plotter
{
 private:
  /* disallow copying and assignment */
  GIFPlotter (const GIFPlotter& oldplotter);  
  GIFPlotter& operator= (const GIFPlotter& oldplotter);
 public:
  /* ctors (old-style, not thread-safe) */
  GIFPlotter (FILE *infile, FILE *outfile, FILE *errfile);
  GIFPlotter (FILE *outfile);
  GIFPlotter (istream& in, ostream& out, ostream& err);
  GIFPlotter (ostream& out);
  GIFPlotter ();
  /* ctors (new-style, thread-safe) */
  GIFPlotter (FILE *infile, FILE *outfile, FILE *errfile, PlotterParams &params);
  GIFPlotter (FILE *outfile, PlotterParams &params);
  GIFPlotter (istream& in, ostream& out, ostream& err, PlotterParams &params);
  GIFPlotter (ostream& out, PlotterParams &params);
  GIFPlotter (PlotterParams &params);
  /* dtor */
  virtual ~GIFPlotter ();
 protected:
  /* protected methods (overriding Plotter methods) */
  bool begin_page (void);
  bool erase_page (void);
  bool end_page (void);
  void paint_point (void);
  void initialize (void);
  void terminate (void);
  void paint_path (void);
  bool paint_paths (void);
  /* GIFPlotter-specific internal functions */
  unsigned char _i_new_color_index (int red, int green, int blue);
  int _i_scan_pixel (void);
  void _i_delete_image (void);
  void _i_draw_elliptic_arc (plPoint p0, plPoint p1, plPoint pc);
  void _i_draw_elliptic_arc_2 (plPoint p0, plPoint p1, plPoint pc);
  void _i_draw_elliptic_arc_internal (int xorigin, int yorigin, unsigned int squaresize_x, unsigned int squaresize_y, int startangle, int anglerange);
  void _i_new_image (void);
  void _i_set_bg_color (void);
  void _i_set_fill_color (void);
  void _i_set_pen_color (void);
  void _i_start_scan (void);
  void _i_write_gif_header (void);
  void _i_write_gif_image (void);
  void _i_write_gif_trailer (void);
  void _i_write_short_int (unsigned int i);
  /* GIFPlotter-specific data members */
  int i_xn, i_yn;		/* bitmap dimensions */
  int i_num_pixels;		/* total pixels (used by scanner) */
  bool i_animation;		/* animated (multi-image) GIF? */
  int i_iterations;		/* number of times GIF should be looped */
  int i_delay;			/* delay after image, in 1/100 sec units */
  bool i_interlace;		/* interlaced GIF? */
  bool i_transparent;		/* transparent GIF? */
  plColor i_transparent_color;	/* if so, transparent color (24-bit RGB) */
  voidptr_t i_arc_cache_data;	/* pointer to cache (used by miPolyArc_r) */
  int i_transparent_index;	/* D: transparent color index (if any) */
  voidptr_t i_painted_set;	/* D: libxmi's canvas (a (miPaintedSet *)) */
  voidptr_t i_canvas;		/* D: libxmi's canvas (a (miCanvas *)) */
  plColor i_colormap[256];	/* D: frame colormap (containing 24-bit RGBs)*/
  int i_num_color_indices;	/* D: number of color indices allocated */
  bool i_frame_nonempty;	/* D: something drawn in current frame? */
  int i_bit_depth;		/* D: bit depth (ceil(log2(num_indices))) */
  int i_pixels_scanned;		/* D: number that scanner has scanned */
  int i_pass;			/* D: scanner pass (used if interlacing) */
  plIntPoint i_hot;		/* D: scanner hot spot */
  plColor i_global_colormap[256]; /* D: colormap for first frame (stashed) */
  int i_num_global_color_indices;/* D: number of indices in global colormap */
  bool i_header_written;	/* D: GIF header written yet? */
};

#ifndef X_DISPLAY_MISSING
/* The XDrawablePlotter class, which draws into one or two X drawables */
class XDrawablePlotter : public Plotter
{
 private:
  /* disallow copying and assignment */
  XDrawablePlotter (const XDrawablePlotter& oldplotter);  
  XDrawablePlotter& operator= (const XDrawablePlotter& oldplotter);
 public:
  /* ctors (old-style, not thread-safe) */
  XDrawablePlotter (FILE *infile, FILE *outfile, FILE *errfile);
  XDrawablePlotter (FILE *outfile);
  XDrawablePlotter (istream& in, ostream& out, ostream& err);
  XDrawablePlotter (ostream& out);
  XDrawablePlotter ();
  /* ctors (new-style, thread-safe) */
  XDrawablePlotter (FILE *infile, FILE *outfile, FILE *errfile, PlotterParams &params);
  XDrawablePlotter (FILE *outfile, PlotterParams &params);
  XDrawablePlotter (istream& in, ostream& out, ostream& err, PlotterParams &params);
  XDrawablePlotter (ostream& out, PlotterParams &params);
  XDrawablePlotter (PlotterParams &params);
  /* dtor */
  virtual ~XDrawablePlotter ();
 protected:
  /* protected methods (overriding Plotter methods) */
  bool begin_page (void);
  bool erase_page (void);
  bool end_page (void);
  bool flush_output (void);
  bool path_is_flushable (void);
  void push_state (void);
  void pop_state (void);
  void paint_point (void);
  void initialize (void);
  void terminate (void);
  void paint_path (void);
  bool paint_paths (void);
  void maybe_prepaint_segments (int prev_num_segments);
  double paint_text_string (const unsigned char *s, int h_just, int v_just);
  double get_text_width (const unsigned char *s);
  bool retrieve_font (void);
  /* internal functions that are overridden in the XPlotter class (crocks) */
  virtual void _maybe_get_new_colormap (void);
  virtual void _maybe_handle_x_events (void);
  /* other XDrawablePlotter-specific internal functions */
  bool _x_retrieve_color (XColor *rgb_ptr);
  bool _x_select_font (const char *name, bool is_zero[4], const unsigned char *s);
  bool _x_select_font_carefully (const char *name, bool is_zero[4], const unsigned char *s);
  bool _x_select_xlfd_font_carefully (const char *x_name, const char *x_name_alt, double user_size, double rotation);
  void _x_add_gcs_to_first_drawing_state (void);
  void _x_delete_gcs_from_first_drawing_state (void);
  void _x_draw_elliptic_arc (plPoint p0, plPoint p1, plPoint pc);
  void _x_draw_elliptic_arc_2 (plPoint p0, plPoint p1, plPoint pc);
  void _x_draw_elliptic_arc_internal (int xorigin, int yorigin, unsigned int squaresize_x, unsigned int squaresize_y, int startangle, int anglerange);
  void _x_set_attributes (int x_gc_type);
  void _x_set_bg_color (void);
  void _x_set_fill_color (void);
  void _x_set_font_dimensions (bool is_zero[4]);
  void _x_set_pen_color (void);
  /* XDrawablePlotter-specific data members */
  Display *x_dpy;		/* X display */
  Visual *x_visual;		/* X visual */
  Drawable x_drawable1;		/* an X drawable (e.g. a pixmap) */
  Drawable x_drawable2;		/* an X drawable (e.g. a window) */
  Drawable x_drawable3;		/* graphics buffer, if double buffering */
  int x_double_buffering;	/* double buffering type (if any) */
  long int x_max_polyline_len;	/* limit on polyline len (X display-specific)*/
  plFontRecord *x_fontlist;	/* D: head of list of retrieved X fonts */
  plColorRecord *x_colorlist;	/* D: head of list of retrieved X color cells*/
  Colormap x_cmap;		/* D: colormap (dynamic only for XPlotters) */
  int x_cmap_type;		/* D: colormap type (orig./copied/bad) */
  bool x_colormap_warning_issued; /* D: issued warning on colormap filling up*/
  bool x_bg_color_warning_issued; /* D: issued warning on bg color */
  int x_paint_pixel_count;	/* D: times point() is invoked to set a pixel*/
};

/* The XPlotter class, which pops up a window and draws into it */
class XPlotter : public XDrawablePlotter
{
 private:
  /* disallow copying and assignment */
  XPlotter (const XPlotter& oldplotter);  
  XPlotter& operator= (const XPlotter& oldplotter);
 public:
  /* ctors (old-style, not thread-safe) */
  XPlotter (FILE *infile, FILE *outfile, FILE *errfile);
  XPlotter (FILE *outfile);
  XPlotter (istream& in, ostream& out, ostream& err);
  XPlotter (ostream& out);
  XPlotter ();
  /* ctors (new-style, thread-safe) */
  XPlotter (FILE *infile, FILE *outfile, FILE *errfile, PlotterParams &params);
  XPlotter (FILE *outfile, PlotterParams &params);
  XPlotter (istream& in, ostream& out, ostream& err, PlotterParams &params);
  XPlotter (ostream& out, PlotterParams &params);
  XPlotter (PlotterParams &params);
  /* dtor */
  virtual ~XPlotter ();
 protected:
  /* protected methods (overriding XDrawablePlotter methods) */
  bool begin_page (void);
  bool erase_page (void);
  bool end_page (void);
  void initialize (void);
  void terminate (void);
  /* internal functions that override XDrawablePlotter functions (crocks) */
  void _maybe_get_new_colormap (void);
  void _maybe_handle_x_events (void);
  /* other XPlotter-specific internal functions */
  void _y_set_data_for_quitting (void);
  /* XPlotter-specific data members (non-static) */
  XtAppContext y_app_con;	/* application context */
  Widget y_toplevel;		/* toplevel widget */
  Widget y_canvas;		/* Label widget */
  Drawable y_drawable4;		/* used for server-side double buffering */
  bool y_auto_flush;		/* do an XFlush() after each drawing op? */
  bool y_vanish_on_delete;	/* window(s) disappear on Plotter deletion? */
  pid_t *y_pids;		/* D: list of pids of forked-off processes */
  int y_num_pids;		/* D: number of pids in list */
  int y_event_handler_count;	/* D: times that event handler is invoked */
  /* XPlotter-specific data members (static) */
  static XPlotter **_xplotters;	/* D: sparse array of XPlotter instances */
  static int _xplotters_len;	/* D: length of sparse array */
};
#endif /* not X_DISPLAY_MISSING */
#endif /* not NOT_LIBPLOTTER */


/***********************************************************************/

/* Useful definitions, included in both plot.h and plotter.h. */

#ifndef _PL_LIBPLOT_USEFUL_DEFS
#define _PL_LIBPLOT_USEFUL_DEFS 1

/* Symbol types for the marker() function, extending over the range 0..31.
   (1 through 5 are the same as in the GKS [Graphical Kernel System].)

   These are now defined as enums rather than ints.  Cast them to ints if
   necessary. */
enum 
{ M_NONE, M_DOT, M_PLUS, M_ASTERISK, M_CIRCLE, M_CROSS, 
  M_SQUARE, M_TRIANGLE, M_DIAMOND, M_STAR, M_INVERTED_TRIANGLE, 
  M_STARBURST, M_FANCY_PLUS, M_FANCY_CROSS, M_FANCY_SQUARE, 
  M_FANCY_DIAMOND, M_FILLED_CIRCLE, M_FILLED_SQUARE, M_FILLED_TRIANGLE, 
  M_FILLED_DIAMOND, M_FILLED_INVERTED_TRIANGLE, M_FILLED_FANCY_SQUARE,
  M_FILLED_FANCY_DIAMOND, M_HALF_FILLED_CIRCLE, M_HALF_FILLED_SQUARE,
  M_HALF_FILLED_TRIANGLE, M_HALF_FILLED_DIAMOND,
  M_HALF_FILLED_INVERTED_TRIANGLE, M_HALF_FILLED_FANCY_SQUARE,
  M_HALF_FILLED_FANCY_DIAMOND, M_OCTAGON, M_FILLED_OCTAGON 
};

/* ONE-BYTE OPERATION CODES FOR GNU METAFILE FORMAT. These are now defined
   as enums rather than ints.  Cast them to ints if necessary.

   There are 85 currently recognized op codes.  The first 10 date back to
   Unix plot(5) format. */

enum
{  
/* 10 op codes for primitive graphics operations, as in Unix plot(5) format. */
  O_ARC		=	'a',  
  O_CIRCLE	=	'c',  
  O_CONT	=	'n',
  O_ERASE	=	'e',
  O_LABEL	=	't',
  O_LINEMOD	=	'f',
  O_LINE	=	'l',
  O_MOVE	=	'm',
  O_POINT	=	'p',
  O_SPACE	=	's',
  
/* 42 op codes that are GNU extensions */
  O_ALABEL	=	'T',
  O_ARCREL	=	'A',
  O_BEZIER2	=       'q',
  O_BEZIER2REL	=       'r',
  O_BEZIER3	=       'y',
  O_BEZIER3REL	=       'z',
  O_BGCOLOR	=	'~',
  O_BOX		=	'B',	/* not an op code in Unix plot(5) */
  O_BOXREL	=	'H',
  O_CAPMOD	=	'K',
  O_CIRCLEREL	=	'G',
  O_CLOSEPATH	=	'k',
  O_CLOSEPL	=	'x',	/* not an op code in Unix plot(5) */
  O_COMMENT	=	'#',
  O_CONTREL	=	'N',
  O_ELLARC	=	'?',
  O_ELLARCREL	=	'/',
  O_ELLIPSE	=	'+',
  O_ELLIPSEREL	=	'=',
  O_ENDPATH	=	'E',
  O_ENDSUBPATH	=	']',
  O_FILLTYPE	=	'L',
  O_FILLCOLOR	=	'D',
  O_FILLMOD	=	'g',
  O_FONTNAME	=	'F',
  O_FONTSIZE	=	'S',
  O_JOINMOD	=	'J',
  O_LINEDASH	= 	'd',
  O_LINEREL	=	'I',
  O_LINEWIDTH	=	'W',
  O_MARKER	=	'Y',
  O_MARKERREL	=	'Z',
  O_MOVEREL	=	'M',
  O_OPENPL	=	'o',	/* not an op code in Unix plot(5) */
  O_ORIENTATION	=	'b',
  O_PENCOLOR	=	'-',
  O_PENTYPE	=	'h',
  O_POINTREL	=	'P',
  O_RESTORESTATE=	'O',
  O_SAVESTATE	=	'U',
  O_SPACE2	=	':',
  O_TEXTANGLE	=	'R',

/* 30 floating point counterparts to many of the above.  They are not even
   slightly mnemonic. */
  O_FARC	=	'1',
  O_FARCREL	=	'2',
  O_FBEZIER2	=       '`',
  O_FBEZIER2REL	=       '\'',
  O_FBEZIER3	=       ',',
  O_FBEZIER3REL	=       '.',
  O_FBOX	=	'3',
  O_FBOXREL	=	'4',
  O_FCIRCLE	=	'5',
  O_FCIRCLEREL	=	'6',
  O_FCONT	=	')',
  O_FCONTREL	=	'_',
  O_FELLARC	=	'}',
  O_FELLARCREL	=	'|',
  O_FELLIPSE	=	'{',
  O_FELLIPSEREL	=	'[',
  O_FFONTSIZE	=	'7',
  O_FLINE	=	'8',
  O_FLINEDASH	= 	'w',
  O_FLINEREL	=	'9',
  O_FLINEWIDTH	=	'0',
  O_FMARKER	=	'!',
  O_FMARKERREL	=	'@',
  O_FMOVE	=	'$',
  O_FMOVEREL	=	'%',
  O_FPOINT	=	'^',
  O_FPOINTREL	=	'&',
  O_FSPACE	=	'*',
  O_FSPACE2	=	';',
  O_FTEXTANGLE	=	'(',

/* 3 op codes for floating point operations with no integer counterpart */
  O_FCONCAT		=	'\\',
  O_FMITERLIMIT		=	'i',
  O_FSETMATRIX		=	'j'
};

#endif /* not _PL_LIBPLOT_USEFUL_DEFS */

/***********************************************************************/

#endif /* not _PLOTTER_H_ */
