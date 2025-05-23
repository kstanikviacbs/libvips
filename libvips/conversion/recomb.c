/* recomb.c ... pass an image though a matrix *
 * 21/6/95 JC
 *	- mildly modernised
 * 14/3/96 JC
 *	- better error checks, partial
 * 4/11/09
 * 	- gtkdoc
 * 9/11/11
 * 	- redo as a class
 */

/*

	Copyright (C) 1991-2005 The National Gallery

	This library is free software; you can redistribute it and/or
	modify it under the terms of the GNU Lesser General Public
	License as published by the Free Software Foundation; either
	version 2.1 of the License, or (at your option) any later version.

	This library is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
	Lesser General Public License for more details.

	You should have received a copy of the GNU Lesser General Public
	License along with this library; if not, write to the Free Software
	Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
	02110-1301  USA

 */

/*

	These files are distributed with VIPS - http://www.vips.ecs.soton.ac.uk

 */

/*
#define DEBUG
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /*HAVE_CONFIG_H*/
#include <glib/gi18n-lib.h>

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <vips/vips.h>
#include <vips/internal.h>

#include "pconversion.h"

typedef struct _VipsRecomb {
	VipsConversion parent_instance;

	VipsImage *in;
	VipsImage *m;

	/* Our input matrix as a one-band double.
	 */
	VipsImage *coeff;

} VipsRecomb;

typedef VipsConversionClass VipsRecombClass;

G_DEFINE_TYPE(VipsRecomb, vips_recomb, VIPS_TYPE_CONVERSION);

/* Inner loop.
 */
#define LOOP(IN, OUT) \
	{ \
		IN *restrict p = (IN *) in; \
		OUT *restrict q = (OUT *) out; \
\
		for (x = 0; x < out_region->valid.width; x++) { \
			double *restrict m = VIPS_MATRIX(recomb->coeff, 0, 0); \
\
			for (v = 0; v < mheight; v++) { \
				double t; \
\
				t = 0.0; \
\
				for (u = 0; u < mwidth; u++) \
					t += m[u] * p[u]; \
\
				q[v] = (OUT) t; \
				m += mwidth; \
			} \
\
			p += mwidth; \
			q += mheight; \
		} \
	}

static int
vips_recomb_gen(VipsRegion *out_region,
	void *seq, void *a, void *b, gboolean *stop)
{
	VipsRegion *ir = (VipsRegion *) seq;
	VipsRecomb *recomb = (VipsRecomb *) b;
	VipsImage *im = recomb->in;
	int mwidth = recomb->m->Xsize;
	int mheight = recomb->m->Ysize;

	int y, x, u, v;

	if (vips_region_prepare(ir, &out_region->valid))
		return -1;

	for (y = 0; y < out_region->valid.height; y++) {
		VipsPel *in = VIPS_REGION_ADDR(ir,
			out_region->valid.left, out_region->valid.top + y);
		VipsPel *out = VIPS_REGION_ADDR(out_region,
			out_region->valid.left, out_region->valid.top + y);

		switch (vips_image_get_format(im)) {
		case VIPS_FORMAT_UCHAR:
			LOOP(unsigned char, float);
			break;
		case VIPS_FORMAT_CHAR:
			LOOP(signed char, float);
			break;
		case VIPS_FORMAT_USHORT:
			LOOP(unsigned short, float);
			break;
		case VIPS_FORMAT_SHORT:
			LOOP(signed short, float);
			break;
		case VIPS_FORMAT_UINT:
			LOOP(unsigned int, float);
			break;
		case VIPS_FORMAT_INT:
			LOOP(signed int, float);
			break;
		case VIPS_FORMAT_FLOAT:
			LOOP(float, float);
			break;
		case VIPS_FORMAT_DOUBLE:
			LOOP(double, double);
			break;

		default:
			g_assert_not_reached();
		}
	}

	return 0;
}

static int
vips_recomb_build(VipsObject *object)
{
	VipsObjectClass *class = VIPS_OBJECT_GET_CLASS(object);
	VipsConversion *conversion = (VipsConversion *) object;
	VipsRecomb *recomb = (VipsRecomb *) object;
	VipsImage **t = (VipsImage **) vips_object_local_array(object, 2);

	VipsImage *in;

	if (VIPS_OBJECT_CLASS(vips_recomb_parent_class)->build(object))
		return -1;

	in = recomb->in;

	if (vips_image_decode(in, &t[0]))
		return -1;
	in = t[0];

	if (vips_check_noncomplex(class->nickname, in))
		return -1;
	if (vips_image_pio_input(recomb->m) ||
		vips_check_uncoded(class->nickname, recomb->m) ||
		vips_check_noncomplex(class->nickname, recomb->m) ||
		vips_check_mono(class->nickname, recomb->m))
		return -1;
	if (in->Bands != recomb->m->Xsize) {
		vips_error(class->nickname,
			"%s", _("bands in must equal matrix width"));
		return -1;
	}

	if (vips_check_matrix(class->nickname, recomb->m, &t[1]))
		return -1;
	recomb->coeff = t[1];

	if (vips_image_pipelinev(conversion->out,
			VIPS_DEMAND_STYLE_THINSTRIP, in, NULL))
		return -1;

	conversion->out->Bands = recomb->m->Ysize;
	if (vips_band_format_isint(in->BandFmt))
		conversion->out->BandFmt = VIPS_FORMAT_FLOAT;

	if (vips_image_generate(conversion->out,
			vips_start_one, vips_recomb_gen, vips_stop_one,
			in, recomb))
		return -1;

	return 0;
}

static void
vips_recomb_class_init(VipsRecombClass *class)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS(class);
	VipsObjectClass *object_class = (VipsObjectClass *) class;
	VipsOperationClass *operation_class = VIPS_OPERATION_CLASS(class);

	gobject_class->set_property = vips_object_set_property;
	gobject_class->get_property = vips_object_get_property;

	object_class->nickname = "recomb";
	object_class->description = _("linear recombination with matrix");
	object_class->build = vips_recomb_build;

	operation_class->flags = VIPS_OPERATION_SEQUENTIAL;

	VIPS_ARG_IMAGE(class, "in", 0,
		_("Input"),
		_("Input image argument"),
		VIPS_ARGUMENT_REQUIRED_INPUT,
		G_STRUCT_OFFSET(VipsRecomb, in));

	VIPS_ARG_IMAGE(class, "m", 102,
		_("M"),
		_("Matrix of coefficients"),
		VIPS_ARGUMENT_REQUIRED_INPUT,
		G_STRUCT_OFFSET(VipsRecomb, m));
}

static void
vips_recomb_init(VipsRecomb *recomb)
{
}

/**
 * vips_recomb: (method)
 * @in: input image
 * @out: (out): output image
 * @m: recombination matrix
 * @...: `NULL`-terminated list of optional named arguments
 *
 * This operation recombines an image's bands. Each pixel in @in is treated as
 * an n-element vector, where n is the number of bands in @in, and multiplied by
 * the n x m matrix @m to produce the m-band image @out.
 *
 * @out is always float, unless @in is double, in which case @out is double
 * too. No complex images allowed.
 *
 * It's useful for various sorts of colour space conversions.
 *
 * ::: seealso
 *     [method@Image.bandmean].
 *
 * Returns: 0 on success, -1 on error
 */
int
vips_recomb(VipsImage *in, VipsImage **out, VipsImage *m, ...)
{
	va_list ap;
	int result;

	va_start(ap, m);
	result = vips_call_split("recomb", ap, in, out, m);
	va_end(ap);

	return result;
}
