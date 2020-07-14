/*******************************************************************************
* Copyright (c) 2018-2020 Cadence Design Systems, Inc.
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to use this Software with Cadence processor cores only and
* not with any other processors and platforms, subject to
* the following conditions:
*
* The above copyright notice and this permission notice shall be included
* in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

******************************************************************************/
#include "common_fpu.h"
#include "xa_type_def.h"
#include "xa_nnlib_kernels_api.h"
#include "xa_nn_maxpool_state.h"
#include "xa_nnlib_err_chk.h"
#include <math.h>

#if !HAVE_VFPU
DISCARD_FUN_FOR_NONVOID_RETURN(WORD32, xa_nn_maxpool_f32,(
    FLOAT32* __restrict__ p_out,
    const FLOAT32* __restrict__ p_inp,
    WORD32  input_height,
    WORD32  input_width,
    WORD32  input_channels,
    WORD32  kernel_height,
    WORD32  kernel_width,
    WORD32  x_stride,
    WORD32  y_stride,
    WORD32  x_padding,
    WORD32  y_padding,
    WORD32  out_height,
    WORD32  out_width,
    WORD32  out_data_format,
    VOID *p_scratch))
#else /* #if !HAVE_VFPU */

#define INCR_N_ROW(ptr, n) \
    ptr = (xtfloatx2 *)((FLOAT32 *)(ptr) + (n) * (input_width));

#define INCR_ROW_IF_HEIGHT(ptr, height) \
        if(height) \
        { \
            INCR_N_ROW(ptr, 1); \
            height--; \
        }

#define CIRC_INCR_N_ROW(ptr, n) \
    AE_ADDCIRC16X4_XC((ae_int16x4 *)ptr, (n * input_width) * sizeof(FLOAT32));

#define CIRC_INCR_ROW_IF_HEIGHT(ptr, height) \
        if(height) \
        { \
            CIRC_INCR_N_ROW(ptr, 1); \
            height--; \
        }

#define INC_1_IF_WIDTH(ptr, width) \
        if(width) \
        { \
            ptr = (xtfloatx2 *)((FLOAT32 *)ptr + 1); \
            width--; \
        }

/* Max pooling without using extra copy of input data 
 * Works with unaligned input, output.
 */

static void maxpool_f32(
    FLOAT32* __restrict__ p_out,
    const FLOAT32* __restrict__ p_inp,
    WORD32  input_height,
    WORD32   input_width,
    WORD32   kernel_height,
    WORD32   kernel_width,
    WORD32   x_stride,
    WORD32   y_stride,
    WORD32  x_padding,
    WORD32  y_padding,
    WORD32   out_height,
    WORD32   out_width,
    pVOID    p_scratch_in)
{
    FLOAT32 *p_scratch = (FLOAT32 *)(p_scratch_in);

    int itr_oh, itr_ow;
    int left_pad_aligned, right_pad, total_out_width, scratch_width;
    const xtfloatx2 * p_src1, * p_src2, * p_src3; 
    const xtfloatx2 * __restrict p_src1_temp, * __restrict p_src2_temp, * __restrict p_src3_temp; 
    xtfloatx2 *p_dst, *p_dst_temp;
    ae_valignx2 align_src1, align_src2, align_src3;
    int i;
    FLOAT32 *p_dst_pad;


    left_pad_aligned = ALIGNED_SIZE(x_padding, ALIGNMENT/sizeof(FLOAT32));

    /* Left padding of temporary output with min_value */
    p_dst_pad = p_scratch;
    for(i = 0; i < left_pad_aligned; i++)
    {
        p_dst_pad[i] = -INFINITY;
    }

    total_out_width = XT_MAX(input_width + x_padding, (out_width - 1) * x_stride + kernel_width); 
    right_pad = total_out_width - (x_padding + input_width);

    /* Right padding of temporary output with min_value,
     * add kernel_width values more for the aligning load operations */
    p_dst_pad = p_scratch + left_pad_aligned + input_width;
    for(i = 0; i < right_pad + kernel_width; i++)
    {
        p_dst_pad[i] = -INFINITY;
    }

    for(itr_oh = 0; itr_oh < out_height; itr_oh++)
    {
        int pool_height, pool_width; 
        int start_row, end_row;

        /* Pool height processing */

        /* Compare the input rows for the required pooling height and store on scratch */
        start_row  = itr_oh * y_stride - y_padding;
        end_row = start_row + kernel_height;
        LIMIT(start_row , 0, input_height);
        LIMIT(end_row , 0, input_height);

        pool_height = end_row - start_row;

        p_dst = (xtfloatx2 *)((FLOAT32 *)p_scratch + left_pad_aligned);

        if(pool_height)
        {
            p_src1 = (const xtfloatx2 *)p_inp;
            INCR_N_ROW(p_src1, start_row); 
            pool_height--;

            p_src2 = p_src1;
            INCR_ROW_IF_HEIGHT(p_src2, pool_height); 

            p_src3 = p_src2;
            INCR_ROW_IF_HEIGHT(p_src3, pool_height); 

            /* Compare three rows per iteration */
            do
            {
                p_dst_temp = p_dst;
                p_src1_temp = p_src1;
                p_src2_temp = p_src2;
                p_src3_temp = p_src3;

                /* prime */
                align_src1 = AE_LA128_PP(p_src1_temp);
                align_src2 = AE_LA128_PP(p_src2_temp);
                align_src3 = AE_LA128_PP(p_src3_temp);

                for(i = 0; i < (input_width >> 2); i++)
                {
                    xtfloatx2 temp, i1, i2, i3;
                    xtfloatx2 j1, j2, j3;

                    AE_LASX2X2_IP(i1, j1, align_src1, (const xtfloatx4 *)p_src1_temp);
                    AE_LASX2X2_IP(i2, j2, align_src2, (const xtfloatx4 *)p_src2_temp);
                    AE_LASX2X2_IP(i3, j3, align_src3, (const xtfloatx4 *)p_src3_temp);

                    temp = XT_MAX_SX2(i1, i2);
                    i1 = XT_MAX_SX2(temp, i3);
                    temp = XT_MAX_SX2(j1, j2);
                    j1 = XT_MAX_SX2(temp, j3);

                    AE_SSX2X2_IP(i1, j1, (xtfloatx4 *)p_dst_temp, 16);
                }

                /* reminder loop for input_width */
                for(i = 0; i < (input_width & 3); i++)
                {
#if 1
                    xtfloatx2 temp, i1, i2, i3, out;
                    i1 = ((const FLOAT32 *)p_src1_temp)[i];
                    i2 = ((const FLOAT32 *)p_src2_temp)[i];
                    i3 = ((const FLOAT32 *)p_src3_temp)[i];

                    temp = XT_MAX_SX2(i1, i2);
                    out = XT_MAX_SX2(temp, i3);

                    ((FLOAT32 *)p_dst_temp)[i] = out;
#else
                    xtfloat temp, i1, i2, i3, out;

                    XT_LSIP(i1, (const xtfloat *)p_src1_temp, sizeof(xtfloat));
                    XT_LSIP(i2, (const xtfloat *)p_src2_temp, sizeof(xtfloat));
                    XT_LSIP(i3, (const xtfloat *)p_src3_temp, sizeof(xtfloat));

                    temp = XT_MAX_S(i1, i2);
                    out  = XT_MAX_S(temp, i3);

                    XT_SSIP(i1, (xtfloat *)p_dst_temp, sizeof(xtfloat));
#endif
                }


                if(!pool_height)
                    break;

                p_src1 = p_dst;

                p_src2 = p_src3;
                INCR_ROW_IF_HEIGHT(p_src2, pool_height); 

                p_src3 = p_src2;
                INCR_ROW_IF_HEIGHT(p_src3, pool_height); 

            }while(1);
        }
        else
        {
            /* If there is no valid input present, fill the output with min_value */
            p_dst_pad = p_scratch + left_pad_aligned ;
            for(i = 0; i < input_width; i++)
            {
                p_dst_pad[i] = -INFINITY;
            }
        }

        /* Pool width processing */

        /* On scratch, compare width-wise with padding*/
        total_out_width = ALIGNED_SIZE(left_pad_aligned + input_width + right_pad + kernel_width, ALIGNMENT/sizeof(FLOAT32));
        scratch_width = x_padding + input_width + right_pad;
        p_dst = (xtfloatx2 *)((FLOAT32 *)p_scratch + total_out_width);
        pool_width = kernel_width;

        p_src1 = (const xtfloatx2 *)((FLOAT32 *)p_scratch + left_pad_aligned - x_padding);
        pool_width--;

        p_src2 = p_src1;
        INC_1_IF_WIDTH(p_src2, pool_width);

        p_src3 = p_src2;
        INC_1_IF_WIDTH(p_src3, pool_width);

        do
        {
            p_dst_temp = p_dst;
            p_src1_temp = p_src1;
            p_src2_temp = p_src2;
            p_src3_temp = p_src3;

            /* prime */
            align_src1 = AE_LA128_PP(p_src1_temp);
            align_src2 = AE_LA128_PP(p_src2_temp);
            align_src3 = AE_LA128_PP(p_src3_temp);

            for(i = 0; i < (scratch_width >> 2); i++)
            {
                xtfloatx2 temp , src1, src2, src3;
                xtfloatx2 j1, j2, j3;

                AE_LASX2X2_IP(src1, j1, align_src1, (const xtfloatx4 *)p_src1_temp);
                AE_LASX2X2_IP(src2, j2, align_src2, (const xtfloatx4 *)p_src2_temp);
                AE_LASX2X2_IP(src3, j3, align_src3, (const xtfloatx4 *)p_src3_temp);

                temp = XT_MAX_SX2(src1, src2);
                src1 = XT_MAX_SX2(temp, src3);
                temp = XT_MAX_SX2(j1, j2);
                j1 = XT_MAX_SX2(temp, j3);
                
                AE_SSX2X2_IP(src1, j1, (xtfloatx4 *)p_dst_temp, 16);
            }

            /* reminder loop for scratch_width */
             for(i = 0; i < (scratch_width & 3); i++)
             {
#if 1
                xtfloatx2 temp , src1, src2, src3, out;

                src1 = ((const FLOAT32 *)p_src1_temp)[i];
                src2 = ((const FLOAT32 *)p_src2_temp)[i];
                src3 = ((const FLOAT32 *)p_src3_temp)[i];

                temp = XT_MAX_SX2(src1, src2);
                out = XT_MAX_SX2(temp, src3);
                ((FLOAT32 *)p_dst_temp)[i] = out;
#else
                xtfloat temp, i1, i2, i3, out;

                XT_LSIP(i1, (const xtfloat *)p_src1_temp, sizeof(xtfloat));
                XT_LSIP(i2, (const xtfloat *)p_src2_temp, sizeof(xtfloat));
                XT_LSIP(i3, (const xtfloat *)p_src3_temp, sizeof(xtfloat));

                temp = XT_MAX_S(i1, i2);
                out  = XT_MAX_S(temp, i3);

                XT_SSIP(i1, (xtfloat *)p_dst_temp, sizeof(xtfloat));
#endif
             }

            if(!pool_width)
                break;

            /* Setup next iteration */
            p_src1 = p_dst;
            p_src2 = p_src3;
            INC_1_IF_WIDTH(p_src2, pool_width);
            p_src3 = p_src2;
            INC_1_IF_WIDTH(p_src3, pool_width);

        }while(1);

        FLOAT32 *ptr_out1 = p_scratch + total_out_width; 
        for(itr_ow = 0; itr_ow < out_width; itr_ow++)
        {
            p_out[itr_oh * out_width * 1 /* out_stride */ + itr_ow * 1 /* out_stride */] = ptr_out1[itr_ow * x_stride]; 
        }
    }
}

WORD32 xa_nn_maxpool_f32(
    FLOAT32* __restrict__ p_out,
    const FLOAT32* __restrict__ p_inp,
    WORD32  input_height,
    WORD32  input_width,
    WORD32  input_channels,
    WORD32  kernel_height,
    WORD32  kernel_width,
    WORD32  x_stride,
    WORD32  y_stride,
    WORD32  x_padding,
    WORD32  y_padding,
    WORD32  out_height,
    WORD32  out_width,
    WORD32  out_data_format,
    VOID   *p_scratch)
{
    WORD32 err = 0;

    /* NULL pointer checks */
    XA_NNLIB_ARG_CHK_PTR(p_out, -1);
    XA_NNLIB_ARG_CHK_PTR(p_inp, -1);
    XA_NNLIB_ARG_CHK_PTR(p_scratch, -1);
    /* Pointer alignment checks */
    XA_NNLIB_ARG_CHK_ALIGN(p_out, ALIGNMENT, -1);
    XA_NNLIB_ARG_CHK_ALIGN(p_inp, ALIGNMENT, -1);
    XA_NNLIB_ARG_CHK_ALIGN(p_scratch, ALIGNMENT, -1);
    /* Basic Parameter checks */
    XA_NNLIB_ARG_CHK_COND((input_height <= 0 || input_width <= 0), -1);
    XA_NNLIB_ARG_CHK_COND((input_channels <= 0), -1);
    XA_NNLIB_ARG_CHK_COND((kernel_height <= 0 || kernel_width <= 0), -1);
    XA_NNLIB_ARG_CHK_COND((y_stride <= 0 || x_stride <= 0), -1);
    XA_NNLIB_ARG_CHK_COND((y_padding < 0 || x_padding < 0), -1);
    XA_NNLIB_ARG_CHK_COND((out_height <= 0 || out_width <= 0), -1);
    XA_NNLIB_ARG_CHK_COND((out_data_format != 1), -1);

    err = xa_nn_maxpool_init(-1
                             ,p_scratch
                             ,input_width
                             ,kernel_height
                             ,kernel_width
                             ,x_stride
                             ,y_stride
                             ,x_padding
                             ,out_width
                             );
    if(err<0)
        return err;

    xa_nn_maxpool_state_t *p_state = (xa_nn_maxpool_state_t *)p_scratch;
    FLOAT32 *p_scratch_in = (FLOAT32 *)(p_state->p_scratch);
    int itr_ic;
    const FLOAT32 *pt_inp;
    FLOAT32 *pt_out;

    for(itr_ic = 0; itr_ic < input_channels; itr_ic++)
    {
        pt_inp = &p_inp[itr_ic * input_height * input_width];
        pt_out = &p_out[itr_ic * out_height * out_width];

        maxpool_f32(pt_out
                ,pt_inp
                ,input_height
                ,input_width
                ,kernel_height
                ,kernel_width
                ,x_stride
                ,y_stride
                ,x_padding
                ,y_padding
                ,out_height
                ,out_width
                ,p_scratch_in
                );
    }
    return 0;
}
#endif /* #if !HAVE_VFPU */