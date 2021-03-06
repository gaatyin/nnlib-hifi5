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
/*************************************************************************************
                          Standard Convolution Application
The test application uses a standard Convolutional Neural Network to recognize a word from 
the input spectrogram given as input where one second audio data is processed in one frame.
The current testbench flow is such that one decision is given per frame.
The model uses kernels from the NN Library and is trained for recognising the word from the
spectrogram. The user can use the Two-word model(yes,no) or the Ten-Word model
(yes,no,up,down,left,right,on,off,stop,go).
The Standard Convolution Application consists of the 
following layers:
1) Input
2) 2D Convolution Layer
3) ReLU Layer
4) Maxpool Layer
5) 2D Convolution Layer
6) ReLU Layer
7) Fully Connected Layer
8) Softmax Layer
The output of the application are probabilistic scores of the recognised words which is displayed on the terminal.
**************************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <xtensa/config/core-isa.h>
#include "xa_type_def.h"
#include "xa_nnlib_api.h"
#include "xt_manage_buffers.h"
#include "cmdline_parser.h"
#include "file_io.h"
#include "xa_nnlib_standards.h"
#include "conv_conv2d_ker_bias.h"
#include "conv_fc_ker_bias.h"


#define PROF_ALLOCATE
#include "xt_profiler.h"

#define MAX_MODEL_LENGTH 20

#define XA_MAX_CMD_LINE_LENGTH 200
#define XA_MAX_ARGS 100
#define PARAMFILE "paramfilesimple_model_conv.txt"

#define VALIDATE_PTR(ptr) if(NULL == ptr) { printf("%s: allocation failed\n", #ptr); return -1;}

#define PRINT_VAR(var)  // printf("%d: %s = %d\n", __LINE__, #var, (int) var); fflush(stdout); fflush(stderr);
#define PRINT_PTR(ptr)  // printf("%d: %s = %p\n", __LINE__, #ptr, (void *) ptr); fflush(stdout); fflush(stderr);

char pb_input_file_path[XA_MAX_CMD_LINE_LENGTH] = "";

typedef struct _test_config_t
{
  int help;
  int precision;
  int frames;
  char model[MAX_MODEL_LENGTH];
  char read_inp_file_name[XA_MAX_CMD_LINE_LENGTH];
}test_config_t;

int default_config(test_config_t *p_cfg)
{
  if(p_cfg)
  { 
    p_cfg->help     = 0;
    p_cfg->precision = -1;
    p_cfg->frames   = 1;  
    strcpy(p_cfg->model, "Yes_No");
    p_cfg->read_inp_file_name[0] = '\0';

    return 0;
  }
  else
  {
    return -1;
  }
}

/*------------------------PARAMETER MACRO DEFINITIONS-----------------------------------*/

// Defining parameters for the first Conv2D layer   

#define CONV1_INPUT_HEIGHT        98 
#define CONV1_INPUT_WIDTH         40  
#define CONV1_INPUT_CHANNELS       1
#define CONV1_KERNEL_HEIGHT       20 
#define CONV1_KERNEL_WIDTH         8 
#define CONV1_OUT_CHANNELS        64 
#define CONV1_OUT_HEIGHT          98
#define CONV1_OUT_WIDTH           40
#define CONV1_OUT_DATA_FORMAT      1
#define CONV1_X_STRIDE             1
#define CONV1_Y_STRIDE             1
#define CONV1_X_PADDING            3
#define CONV1_Y_PADDING            9

// Defining parameters for the Maxpool Layer

#define MAXP_KERNEL_HEIGHT         2
#define MAXP_KERNEL_WIDTH          2
#define MAXP_X_STRIDE              2
#define MAXP_Y_STRIDE              2
#define MAXP_X_PADDING             0
#define MAXP_Y_PADDING             0
#define MAXP_OUT_HEIGHT           49
#define MAXP_OUT_WIDTH            20
#define MAXP_OUT_DATA_FORMAT       1

// Defining parameters for the second Conv2D Layer

#define CONV2_KERNEL_HEIGHT       10 
#define CONV2_KERNEL_WIDTH         4 
#define CONV2_OUT_CHANNELS        64 
#define CONV2_OUT_HEIGHT          49
#define CONV2_OUT_WIDTH           20
#define CONV2_OUT_DATA_FORMAT      0
#define CONV2_X_STRIDE             1
#define CONV2_Y_STRIDE             1
#define CONV2_X_PADDING            1
#define CONV2_Y_PADDING            4

// Defining parameters for the Fully Connected Layer

#define FC_WEIGHT_DEPTH        62720   

/*-------------------------Parsing Command Line Arguments------------------------------*/

void parse_arguments(int argc, char** argv, test_config_t *p_cfg)
{
  int argidx;
  for (argidx=1;argidx<argc;argidx++)
  {
    if(strncmp((argv[argidx]), "-", 1) != 0)
    {
      //err_code = 0;
      printf("Invalid argument: %s at index %d\n",argv[argidx], argidx);
      exit(1);
    }
    ARGTYPE_INDICATE("--help", p_cfg->help);
    ARGTYPE_INDICATE("-help", p_cfg->help);
    ARGTYPE_INDICATE("-h", p_cfg->help);
    ARGTYPE_ONETIME_CONFIG("-frames",p_cfg->frames);
    ARGTYPE_ONETIME_CONFIG("-precision",p_cfg->precision);
    ARGTYPE_STRING("-model",p_cfg->model, MAX_MODEL_LENGTH);
    ARGTYPE_STRING("-read_inp_file_name",p_cfg->read_inp_file_name, XA_MAX_CMD_LINE_LENGTH);
    
    // If arg doesnt match with any of the above supported options, report option as invalid
    printf("Invalid argument: %s\n",argv[argidx]);
    exit(1);
  }
}

void show_usage(void)
{
    printf ("Standard Convolution Application: Speech Commands\n");
    printf ("Usage xt-run <binary> [Options]\n");
    printf("\t-help: Show help\n");
    printf("\t-frames: Positive number; Default=1\n");
    printf("\t-precision: -1 for FLOAT32; Default=-1 FLOAT32\n");
    printf("\t-model: Yes_No, Ten_Word; Default=Yes_No\n");
    printf("\t-read_inp_file_name: Full filename for reading input spectogram \n");
}

/*------------------------------Output Labels: Top 3--------------------------------------------------*/

void label_out(buf1D_t *p_out, int idx)
{
    switch(idx)
    {
	case 0:  printf("_silence_\t\t  %f\n",((float *)p_out->p)[idx]);
		 break;
	case 1:  printf("_unknown_\t\t  %f\n",((float *)p_out->p)[idx]);
		 break;
	case 2:  printf("yes      \t\t  %f\n",((float *)p_out->p)[idx]);
		 break;
	case 3:  printf("no       \t\t  %f\n",((float *)p_out->p)[idx]);
		 break;
	case 4:  printf("up       \t\t  %f\n",((float *)p_out->p)[idx]);
		 break;
	case 5:  printf("down     \t\t  %f\n",((float *)p_out->p)[idx]);
		 break;
	case 6:  printf("left     \t\t  %f\n",((float *)p_out->p)[idx]);
		 break;
	case 7:  printf("right    \t\t  %f\n",((float *)p_out->p)[idx]);
		 break;
	case 8:  printf("on       \t\t  %f\n",((float *)p_out->p)[idx]);
		 break;
	case 9:  printf("off      \t\t  %f\n",((float *)p_out->p)[idx]);
		 break;
	case 10: printf("stop     \t\t  %f\n",((float *)p_out->p)[idx]);
		 break;
	case 11: printf("go       \t\t  %f\n",((float *)p_out->p)[idx]);
		 break;
	default: printf("Invalid Index");
		 break;
    }
}

void top3_label_out(buf1D_t *p_out, int OUT_DEPTH)
{
   int iter, idx_1,idx_2,idx_3;
   float max, second_max, third_max;
     
   idx_1 = idx_2 = idx_3 = 0; 

   if (OUT_DEPTH < 3) 
    { 
        printf(" Invalid Input "); 
        return; 
    } 
   
    third_max = max = second_max =(FLOAT32)0; 
    for (iter = 0; iter < OUT_DEPTH ; iter++) 
    { 
        /* If current element is greater than first*/
        if (((float *)p_out->p)[iter] > max) 
        { 
            third_max = second_max;
	    second_max = max;
            max = ((float *)p_out->p)[iter]; 
            idx_3 = idx_2;
	    idx_2 = idx_1;
	    idx_1 = iter;
        } 
   
        /* If arr[i] is in between first and second then update second  */
        else if (((float *)p_out->p)[iter] > second_max) 
        { 
            third_max = second_max;
            second_max = ((float *)p_out->p)[iter]; 
            idx_3 = idx_2;
            idx_2 = iter;
        } 
   
        else if (((float *)p_out->p)[iter] > third_max){ 
            third_max = ((float *)p_out->p)[iter]; 
	    idx_3 = iter;}
    } 
   
    printf("Word\t\t\t  Score\n");
    label_out(p_out, idx_1);
    label_out(p_out, idx_2);
    label_out(p_out, idx_3);
}; 

/*--------------------------------Conversion: WHD to DWH---------------------------------*/
void convert_whd_to_dwh(buf1D_t *p_maxp_out_dwh, buf1D_t* p_maxp_out, int maxp_out_size){
    int iter,new_iter;
    int offset = MAXP_OUT_HEIGHT*MAXP_OUT_WIDTH;

    for(iter=0;iter<maxp_out_size;iter++){
        new_iter = (iter/offset)+((iter%offset)*CONV1_OUT_CHANNELS);
        ((float *)p_maxp_out_dwh->p)[new_iter] = ((float *)p_maxp_out->p)[iter];
    }
}

    
/*--------------------------------Kernel Macro Definitions-------------------------------*/

#define CONV1_2D(KERNEL, IPREC, OPREC) \
  ((IPREC == p_inp->precision) && (OPREC == p_conv1_out->precision)) {\
    XTPWR_BASIC_PROFILER_START(0); \
    err = xa_nn_##KERNEL##_f32 ( \
        (FLOAT32 *)p_conv1_out->p, (FLOAT32 *) p_inp->p,(FLOAT32 *) conv1_kernel,(FLOAT32 *) conv1_bias, \
        CONV1_INPUT_HEIGHT, CONV1_INPUT_WIDTH, CONV1_INPUT_CHANNELS, \
        CONV1_KERNEL_HEIGHT, CONV1_KERNEL_WIDTH, CONV1_OUT_CHANNELS, \
        CONV1_X_STRIDE, CONV1_Y_STRIDE, CONV1_X_PADDING, CONV1_Y_PADDING, \
        CONV1_OUT_HEIGHT, CONV1_OUT_WIDTH, CONV1_OUT_DATA_FORMAT, p_scratch);\
    XTPWR_BASIC_PROFILER_STOP(0); \
    if(err)\
    {\
      fprintf(stdout, "\nConv1 layer returned error (invalid parameters), Performance numbers may be incorrect!\n\n");\
      pass_count += !err;\
      break;\
    }\
  }  

#define RELU1_VEC(IPREC, OPREC) \
  ((OPREC == p_relu1_out->precision) && (IPREC == p_conv1_out->precision)) {\
    XTPWR_BASIC_PROFILER_START(1); \
    err = xa_nn_vec_relu_f32_f32 ((FLOAT32 *)p_relu1_out->p, (const FLOAT32 *) p_conv1_out->p, \
        f32_pos_inf, (WORD32)conv1_out_size); \
    XTPWR_BASIC_PROFILER_STOP(1); \
    if(err)\
    {\
      fprintf(stdout, "\nRelu returned error (invalid parameters), Performance numbers may be incorrect!\n\n");\
      pass_count += !err;\
      break;\
    }\
  }

#define MAXPOOL(IPREC, OPREC) \
  ((IPREC == p_relu1_out->precision) && (OPREC == p_maxp_out->precision)) {\
    XTPWR_BASIC_PROFILER_START(2); \
    err = xa_nn_maxpool_f32 ( \
        (FLOAT32 *)p_maxp_out->p, (FLOAT32 *) p_relu1_out->p, \
        MAXP_INPUT_HEIGHT, MAXP_INPUT_WIDTH, MAXP_INPUT_CHANNELS, \
        MAXP_KERNEL_HEIGHT, MAXP_KERNEL_WIDTH, \
        MAXP_X_STRIDE, MAXP_Y_STRIDE, MAXP_X_PADDING, MAXP_Y_PADDING, \
        MAXP_OUT_HEIGHT, MAXP_OUT_WIDTH, MAXP_OUT_DATA_FORMAT, MAXP_OUT_DATA_FORMAT, p_scratch);\
    XTPWR_BASIC_PROFILER_STOP(2); \
    if(err)\
    {\
      fprintf(stdout, "\nMaxpool layer returned error (invalid parameters), Performance numbers may be incorrect!\n\n");\
      pass_count += !err;\
      break;\
    }\
  }  

#define CONV2_2D(KERNEL, IPREC, OPREC) \
  ((IPREC == p_maxp_out_dwh->precision) && (OPREC == p_conv2_out->precision)) {\
    XTPWR_BASIC_PROFILER_START(3); \
    err = xa_nn_##KERNEL##_f32 ( \
        (FLOAT32 *)p_conv2_out->p, (FLOAT32 *) p_maxp_out_dwh->p,(FLOAT32 *) conv2_kernel,(FLOAT32 *) conv2_bias, \
        CONV2_INPUT_HEIGHT, CONV2_INPUT_WIDTH, CONV2_INPUT_CHANNELS, \
        CONV2_KERNEL_HEIGHT, CONV2_KERNEL_WIDTH, CONV2_OUT_CHANNELS, \
        CONV2_X_STRIDE, CONV2_Y_STRIDE, CONV2_X_PADDING, CONV2_Y_PADDING, \
        CONV2_OUT_HEIGHT, CONV2_OUT_WIDTH, CONV2_OUT_DATA_FORMAT, p_scratch);\
    XTPWR_BASIC_PROFILER_STOP(3); \
    if(err)\
    {\
      fprintf(stdout, "\nConv2 layer returned error (invalid parameters), Performance numbers may be incorrect!\n\n");\
      pass_count += !err;\
      break;\
    }\
  }  

#define RELU2_VEC(IPREC, OPREC) \
  ((OPREC == p_relu2_out->precision) && (IPREC == p_conv2_out->precision)) {\
    XTPWR_BASIC_PROFILER_START(4); \
    err = xa_nn_vec_relu_f32_f32 ((FLOAT32 *)p_relu2_out->p, (const FLOAT32 *) p_conv2_out->p, \
        f32_pos_inf, (WORD32)conv2_out_size); \
    XTPWR_BASIC_PROFILER_STOP(4); \
    if(err)\
    {\
      fprintf(stdout, "\nRelu returned error (invalid parameters), Performance numbers may be incorrect!\n\n");\
      pass_count += !err;\
      break;\
    }\
  }

#define FC_LAYER(IPREC, OPREC) \
  ((IPREC == p_relu2_out->precision) && (OPREC == p_fc_out->precision)) {\
    XTPWR_BASIC_PROFILER_START(5); \
    err = xa_nn_fully_connected_f32((FLOAT32 *)p_fc_out->p,(FLOAT32 *) fc_kernel, (FLOAT32 *)p_relu2_out->p, \
        (FLOAT32 *) fc_bias, FC_WEIGHT_DEPTH,FC_OUT_DEPTH); \
    XTPWR_BASIC_PROFILER_STOP(5); \
    if(err)\
    {\
      fprintf(stdout, "\nFC returned error (invalid parameters), Performance numbers may be incorrect!\n\n");\
      pass_count += !err;\
      break;\
    }\
  }

#define SOFTMAX(IPREC, OPREC) \
  ((OPREC == p_out->precision) && (IPREC == p_fc_out->precision)) {\
    XTPWR_BASIC_PROFILER_START(6); \
    err = xa_nn_vec_softmax_f32_f32((FLOAT32 *)p_out->p, (const FLOAT32 *)p_fc_out->p,  FC_OUT_DEPTH);\
    XTPWR_BASIC_PROFILER_STOP(6); \
    if(err)\
    {\
      fprintf(stdout, "\nSoftmax returned error (invalid parameters), Performance numbers may be incorrect!\n\n");\
      pass_count += !err;\
      break;\
    }\
  }

#if HIFI_VFPU
#define PROCESS_CONV1_2D \
    if CONV1_2D(conv2d_std, -1, -1) \
    else {printf("[Error] Conv1 is not supported\n"); return -1;}
#define PROCESS_RELU1 \
    if RELU1_VEC(-1, -1) \
    else {printf("[Error] ReLU1 is not supported\n"); return -1;}
#define PROCESS_MAXPOOL \
    if MAXPOOL(-1, -1) \
    else {printf("[Error] Maxpool is not supported\n"); return -1;}
#define PROCESS_CONV2_2D \
    if CONV2_2D(conv2d_std, -1, -1) \
    else {printf("[Error] Conv2 is not supported\n"); return -1;}
#define PROCESS_RELU2 \
    if RELU2_VEC(-1, -1) \
    else {printf("[Error] ReLU2 is not supported\n"); return -1;}
#define PROCESS_FC \
    if FC_LAYER(-1, -1) \
    else {printf("[Error] FC is not supported\n"); return -1;}
#define PROCESS_SOFTMAX \
    if SOFTMAX(-1, -1) \
    else {printf("[Error] Softmax is not supported\n"); return -1;}
#else
#define PROCESS_CONV1_2D \
    {printf("[Error] Conv1 is not supported\n"); return -1;}
#define PROCESS_RELU1 \
    {printf("[Error] ReLU1 is not supported\n"); return -1;}
#define PROCESS_MAXPOOL \
    {printf("[Error] Maxpool is not supported\n"); return -1;}
#define PROCESS_CONV2_2D \
    {printf("[Error] Conv2 is not supported\n"); return -1;}
#define PROCESS_RELU2 \
    {printf("[Error] ReLU2 is not supported\n"); return -1;}
#define PROCESS_FC \
    {printf("[Error] FC is not supported\n"); return -1;}
#define PROCESS_SOFTMAX \
    (void) err; \
    (void) f32_pos_inf; \
    (void) pass_count; \
    {printf("[Error] Softmax is not supported\n"); return -1;}
#endif

/*-------------------------------------Process Function----------------------------------------*/

int xa_nn_main_process(int argc, char *argv[])
{

  int frame;
  int err = 0;
  int pass_count=0;
  
  char profiler_name_0[MAX_PROFILER_NAME_LENGTH]; 
  char profiler_name_1[MAX_PROFILER_NAME_LENGTH]; 
  char profiler_name_2[MAX_PROFILER_NAME_LENGTH]; 
  char profiler_name_3[MAX_PROFILER_NAME_LENGTH]; 
  char profiler_name_4[MAX_PROFILER_NAME_LENGTH]; 
  char profiler_name_5[MAX_PROFILER_NAME_LENGTH]; 
  char profiler_name_6[MAX_PROFILER_NAME_LENGTH]; 
  char profiler_params_conv1[MAX_PROFILER_PARAMS_LENGTH]; 
  char profiler_params_relu1[MAX_PROFILER_PARAMS_LENGTH]; 
  char profiler_params_maxp[MAX_PROFILER_PARAMS_LENGTH]; 
  char profiler_params_conv2[MAX_PROFILER_PARAMS_LENGTH]; 
  char profiler_params_relu2[MAX_PROFILER_PARAMS_LENGTH]; 
  char profiler_params_fc[MAX_PROFILER_PARAMS_LENGTH]; 
  char profiler_params_softmax[MAX_PROFILER_PARAMS_LENGTH]; 
  
  
  void *p_scratch;
  int conv1_inp_size, conv1_out_size,maxp_out_size, conv2_out_size;
  test_config_t cfg;
  FLOAT32 *conv1_kernel,*conv1_bias, *conv2_kernel,*conv2_bias,*fc_kernel,*fc_bias;
  FLOAT32  f32_pos_inf = 0x7F800000;

  int MAXP_INPUT_HEIGHT, MAXP_INPUT_WIDTH, MAXP_INPUT_CHANNELS;
  int CONV2_INPUT_HEIGHT, CONV2_INPUT_WIDTH, CONV2_INPUT_CHANNELS;
  int FC_OUT_DEPTH=0;  
    
  buf1D_t *p_inp;
  buf1D_t *p_conv1_out;
  buf1D_t *p_relu1_out;
  buf1D_t *p_maxp_out;
  buf1D_t *p_maxp_out_dwh;
  buf1D_t *p_conv2_out;
  buf1D_t *p_relu2_out;
  buf1D_t *p_fc_out;
  buf1D_t *p_out;
  
  FILE *fptr_inp;

  if(default_config(&cfg))
  {
    return -1;
  }
  
    printf("Parsing CMDLINE\n");
    parse_arguments(argc, argv, &cfg);
    if(1 == cfg.help)
    {
      show_usage();
      return 0;
    }

  // Defining OUT_DEPTH of the FC layer based on the model 
    if(!strcmp(cfg.model,"Yes_No"))
    {
	    FC_OUT_DEPTH = 4;	
    }
    else if(!strcmp(cfg.model,"Ten_Word"))
    {
	    FC_OUT_DEPTH = 12;
    }

    // Assigning variables that connect two kernels

    // CONV1 Layer
    conv1_inp_size = CONV1_INPUT_HEIGHT * CONV1_INPUT_WIDTH * CONV1_INPUT_CHANNELS;
    conv1_out_size = CONV1_OUT_HEIGHT * CONV1_OUT_WIDTH * CONV1_OUT_CHANNELS;

    // MAXPOOL Layer
    MAXP_INPUT_HEIGHT   = CONV1_OUT_HEIGHT;
    MAXP_INPUT_WIDTH    = CONV1_OUT_WIDTH;
    MAXP_INPUT_CHANNELS = CONV1_OUT_CHANNELS;
    maxp_out_size = MAXP_OUT_HEIGHT * MAXP_OUT_WIDTH * MAXP_INPUT_CHANNELS;

    // CONV2 Layer
    CONV2_INPUT_HEIGHT   = MAXP_OUT_HEIGHT;
    CONV2_INPUT_WIDTH    = MAXP_OUT_WIDTH;
    CONV2_INPUT_CHANNELS = MAXP_INPUT_CHANNELS;
    conv2_out_size = CONV2_OUT_HEIGHT * CONV2_OUT_WIDTH * CONV2_OUT_CHANNELS;
    
    //Set Profiler name and profiler params
    strcpy(profiler_name_0,"Standard Conv Application: CONV1 ");
    strcpy(profiler_name_1,"Standard Conv Application: ReLU1 ");
    strcpy(profiler_name_2,"Standard Conv Application: Maxpool ");
    strcpy(profiler_name_3,"Standard Conv Application: CONV2 ");
    strcpy(profiler_name_4,"Standard Conv Application: ReLU2 ");
    strcpy(profiler_name_5,"Standard Conv Application: FC ");
    strcpy(profiler_name_6,"Standard Conv Application: Softmax ");
    sprintf(profiler_params_conv1, "Float32");
    sprintf(profiler_params_relu1, "Float32");
    sprintf(profiler_params_maxp, "Float32");
    sprintf(profiler_params_conv2, "Float32");
    sprintf(profiler_params_relu2, "Float32");
    sprintf(profiler_params_fc, "Float32");
    sprintf(profiler_params_softmax, "Float32");
    strcat(profiler_name_0, profiler_params_conv1);
    strcat(profiler_name_1, profiler_params_relu1);
    strcat(profiler_name_2, profiler_params_maxp);
    strcat(profiler_name_3, profiler_params_conv2);
    strcat(profiler_name_4, profiler_params_relu2);
    strcat(profiler_name_5, profiler_params_fc);
    strcat(profiler_name_6, profiler_params_softmax);
    
    // If VFPU is not supported, return
    if(!HIFI_VFPU)
    {
      printf("%s: NOT TESTED\n", profiler_name_0);
      printf("%s: NOT TESTED\n", profiler_name_1);
      printf("%s: NOT TESTED\n", profiler_name_2);
      printf("%s: NOT TESTED\n", profiler_name_3);
      printf("%s: NOT TESTED\n", profiler_name_4);
      printf("%s: NOT TESTED\n", profiler_name_5);
      printf("%s: NOT TESTED\n", profiler_name_6);
      return 0;
    }

  sprintf(profiler_params_conv1, "input_height=%d, input_width=%d, input_channels=%d, kernel_height=%d, kernel_width=%d, out_channels=%d, out_height=%d, out_width=%d", 
      CONV1_INPUT_HEIGHT, CONV1_INPUT_WIDTH, CONV1_INPUT_CHANNELS, CONV1_KERNEL_HEIGHT, CONV1_KERNEL_WIDTH, CONV1_OUT_CHANNELS, CONV1_OUT_HEIGHT, CONV1_OUT_WIDTH);

  sprintf(profiler_params_relu1, "threshold= +Inf, vec_length=%d", 
      conv1_out_size);

  sprintf(profiler_params_maxp, "input_height=%d, input_width=%d, input_channels=%d, kernel_height=%d, kernel_width=%d, out_height=%d, out_width=%d", 
      MAXP_INPUT_HEIGHT, MAXP_INPUT_WIDTH, MAXP_INPUT_CHANNELS, MAXP_KERNEL_HEIGHT, MAXP_KERNEL_WIDTH, MAXP_OUT_HEIGHT, MAXP_OUT_WIDTH);
  
  sprintf(profiler_params_conv2, "input_height=%d, input_width=%d, input_channels=%d, kernel_height=%d, kernel_width=%d, out_channels=%d, out_height=%d, out_width=%d", 
      CONV2_INPUT_HEIGHT, CONV2_INPUT_WIDTH, CONV2_INPUT_CHANNELS, CONV2_KERNEL_HEIGHT, CONV2_KERNEL_WIDTH, CONV2_OUT_CHANNELS, CONV2_OUT_HEIGHT, CONV2_OUT_WIDTH);
      
  sprintf(profiler_params_relu2, "threshold= +Inf, vec_length=%d", 
      conv2_out_size);

  sprintf(profiler_params_fc, "out_depth=%d, weight_depth=%d", 
      FC_OUT_DEPTH, FC_WEIGHT_DEPTH);
      
  sprintf(profiler_params_softmax, "vec_length=%d", 
      FC_OUT_DEPTH);

  // Open input file for reading data
  fptr_inp = file_open(pb_input_file_path, cfg.read_inp_file_name, "rb", XA_MAX_CMD_LINE_LENGTH);
    
  // Allocate Memory
  p_inp = create_buf1D(conv1_inp_size, cfg.precision);                            VALIDATE_PTR(p_inp);
  p_conv1_out = create_buf1D(conv1_out_size, cfg.precision);                      VALIDATE_PTR(p_conv1_out);
  p_relu1_out = create_buf1D(conv1_out_size, cfg.precision);                      VALIDATE_PTR(p_relu1_out);
  p_maxp_out  = create_buf1D(maxp_out_size, cfg.precision);                       VALIDATE_PTR(p_maxp_out); 
  p_maxp_out_dwh  = create_buf1D(maxp_out_size, cfg.precision);                   VALIDATE_PTR(p_maxp_out_dwh); 
  p_conv2_out = create_buf1D(conv2_out_size, cfg.precision);                      VALIDATE_PTR(p_conv2_out);
  p_relu2_out = create_buf1D(conv2_out_size, cfg.precision);                      VALIDATE_PTR(p_relu2_out);
  p_fc_out = create_buf1D(FC_OUT_DEPTH, cfg.precision);                              VALIDATE_PTR(p_fc_out);
  p_out = create_buf1D(FC_OUT_DEPTH, cfg.precision);                                 VALIDATE_PTR(p_out);
  
  XTPWR_PROFILER_OPEN(0, profiler_name_0, profiler_params_conv1, (conv1_out_size*CONV1_KERNEL_HEIGHT*CONV1_KERNEL_WIDTH*CONV1_INPUT_CHANNELS), "MACs/cyc", 1);
  XTPWR_PROFILER_OPEN(1, profiler_name_1, profiler_params_relu1, 1, NULL, 1);
  XTPWR_PROFILER_OPEN(2, profiler_name_2, profiler_params_maxp, 1, NULL, 1);
  XTPWR_PROFILER_OPEN(3, profiler_name_3, profiler_params_conv2, (conv2_out_size*CONV2_KERNEL_HEIGHT*CONV2_KERNEL_WIDTH*CONV2_INPUT_CHANNELS), "MACs/cyc", 1);
  XTPWR_PROFILER_OPEN(4, profiler_name_4, profiler_params_relu2, 1, NULL, 1);
  XTPWR_PROFILER_OPEN(5, profiler_name_5, profiler_params_fc, (FC_WEIGHT_DEPTH*FC_OUT_DEPTH), "MACs/cyc", 1);
  XTPWR_PROFILER_OPEN(6, profiler_name_6, profiler_params_softmax, 1, NULL, 1);
  
  // Init
  WORD32 conv1_scratch_size, maxp_scratch_size, conv2_scratch_size,max_scratch;

  // Get persistent size and allocate 
  conv1_scratch_size = xa_nn_conv2d_std_getsize(CONV1_INPUT_HEIGHT,CONV1_INPUT_CHANNELS,CONV1_KERNEL_HEIGHT,CONV1_KERNEL_WIDTH,CONV1_Y_STRIDE,CONV1_Y_PADDING,CONV1_OUT_HEIGHT,cfg.precision); PRINT_VAR(conv1_scratch_size)

  maxp_scratch_size = xa_nn_maxpool_getsize(MAXP_INPUT_CHANNELS,cfg.precision,cfg.precision,MAXP_INPUT_HEIGHT,MAXP_INPUT_WIDTH,MAXP_KERNEL_HEIGHT,MAXP_KERNEL_WIDTH,MAXP_X_STRIDE,MAXP_Y_STRIDE,MAXP_X_PADDING,MAXP_Y_PADDING,MAXP_OUT_HEIGHT,MAXP_OUT_WIDTH,MAXP_OUT_DATA_FORMAT,MAXP_OUT_DATA_FORMAT); PRINT_VAR(maxp_scratch_size)

  conv2_scratch_size = xa_nn_conv2d_std_getsize(CONV2_INPUT_HEIGHT,CONV2_INPUT_CHANNELS,CONV2_KERNEL_HEIGHT,CONV2_KERNEL_WIDTH,CONV2_Y_STRIDE,CONV2_Y_PADDING,CONV2_OUT_HEIGHT,cfg.precision); PRINT_VAR(conv2_scratch_size)
  if(conv1_scratch_size > maxp_scratch_size){
      if(conv1_scratch_size > conv2_scratch_size){
          max_scratch = conv1_scratch_size;
      }
      else{
          max_scratch = conv2_scratch_size;
      }
  }
  else if(maxp_scratch_size > conv2_scratch_size){
      max_scratch = maxp_scratch_size;
  }
  else{
      max_scratch = conv2_scratch_size;
  }

  p_scratch = (xa_nnlib_handle_t)malloc(max_scratch); PRINT_PTR(p_scratch)

  fprintf(stdout, "\nScratch size: %d bytes\n", max_scratch);
  
  // Frame processing loop
  for(frame = 0; frame < cfg.frames; frame++)
  {
    // Reading input from the file and pointing the trained weight pointers to the tables,
    // based on the model specified.
    read_buf1D_from_file(fptr_inp, p_inp); 
    if(!strcmp(cfg.model,"Yes_No"))
    {
        conv1_kernel  = (float *)Yes_No_conv1_kernel;
        conv1_bias    = (float *)Yes_No_conv1_bias;
        conv2_kernel  = (float *)Yes_No_conv2_kernel;
        conv2_bias    = (float *)Yes_No_conv2_bias;
        fc_kernel    = (float *)Yes_No_fc_kernel;
        fc_bias      = (float *)Yes_No_fc_bias;
    }
    else if(!strcmp(cfg.model,"Ten_Word"))
    {
        conv1_kernel  = (float *)Ten_Word_conv1_kernel;
        conv1_bias    = (float *)Ten_Word_conv1_bias;
        conv2_kernel  = (float *)Ten_Word_conv2_kernel;
        conv2_bias    = (float *)Ten_Word_conv2_bias;
        fc_kernel    = (float *)Ten_Word_fc_kernel;
        fc_bias      = (float *)Ten_Word_fc_bias;
    }

    // Call the Conv 2D std. kernel 
    PROCESS_CONV1_2D;
    // Applying ReLU kernel to the Conv Output
    PROCESS_RELU1;	   
    // Applying Maxpool kernel to the ReLU output
    PROCESS_MAXPOOL;
    // Convert the Maxpool output in WHD format to DWH
    convert_whd_to_dwh(p_maxp_out_dwh, p_maxp_out, maxp_out_size);
    // Call the Conv 2D std. kernel 
    PROCESS_CONV2_2D;
    // Applying ReLU kernel to the Conv Output
    PROCESS_RELU2;	    
    // Applying ReLU output to the FC layer 
    PROCESS_FC;
    // Applying Softmax to the FC layer output
    PROCESS_SOFTMAX;
    //Update and print the profiler cycles
    XTPWR_PROFILER_UPDATE(0);
    XTPWR_PROFILER_PRINT(0);
    XTPWR_PROFILER_UPDATE(1);
    XTPWR_PROFILER_PRINT(1);
    XTPWR_PROFILER_UPDATE(2);
    XTPWR_PROFILER_PRINT(2);
    XTPWR_PROFILER_UPDATE(3);
    XTPWR_PROFILER_PRINT(3);
    XTPWR_PROFILER_UPDATE(4);
    XTPWR_PROFILER_PRINT(4);
    XTPWR_PROFILER_UPDATE(5);
    XTPWR_PROFILER_PRINT(5);
    XTPWR_PROFILER_UPDATE(6);
    XTPWR_PROFILER_PRINT(6);

    top3_label_out(p_out, FC_OUT_DEPTH);
  }
    XTPWR_PROFILER_AVE_TOTAL(6);

  //Closing Profiler
  XTPWR_PROFILER_CLOSE(0,(frame == cfg.frames));
  XTPWR_PROFILER_CLOSE(1,(frame == cfg.frames));
  XTPWR_PROFILER_CLOSE(2,(frame == cfg.frames));
  XTPWR_PROFILER_CLOSE(3,(frame == cfg.frames));
  XTPWR_PROFILER_CLOSE(4,(frame == cfg.frames));
  XTPWR_PROFILER_CLOSE(5,(frame == cfg.frames));
  XTPWR_PROFILER_CLOSE(6,(frame == cfg.frames));

  // Closing the files
  fclose(fptr_inp);

  // Free all buffers
  free_buf1D(p_inp);
  free_buf1D(p_conv1_out);
  free_buf1D(p_relu1_out);
  free_buf1D(p_maxp_out);
  free_buf1D(p_maxp_out_dwh);
  free_buf1D(p_conv2_out);
  free_buf1D(p_relu2_out);
  free_buf1D(p_fc_out);
  free_buf1D(p_out);
  free(p_scratch);

  return 0;
}

int main (int argc, char *argv[])
{
    FILE *param_file_id;
    int err_code = 0;

    WORD8 curr_cmd[XA_MAX_ARGS * XA_MAX_CMD_LINE_LENGTH];
    WORD32 fargc, curpos;
    WORD32 processcmd = 0;

    char fargv[XA_MAX_ARGS][XA_MAX_CMD_LINE_LENGTH];

    char *pargv[XA_MAX_ARGS+1];

    if(argc == 1)
    {
        param_file_id = fopen(PARAMFILE, "r");
        if (param_file_id == NULL)
        {
            err_code = -1;
            printf("Error opening Parameter file for reading %s\n",PARAMFILE);
            exit(1);
        }

        // Process one line at a time 
        while(fgets((char *)curr_cmd, XA_MAX_ARGS * XA_MAX_CMD_LINE_LENGTH, param_file_id))
        {
            curpos = 0;
            fargc = 0;
            // if it is not a param_file command and if 
            // CLP processing is not enabled 
            if(curr_cmd[0] != '@' && !processcmd)
            {   // skip it 
                continue;
            }

            while(sscanf((const char *)curr_cmd + curpos, "%s", fargv[fargc]) != EOF)
            {
                if(fargv[0][0]=='/' && fargv[0][1]=='/')
                    break;
                if(strcmp(fargv[0], "@echo") == 0)
                    break;
                if(strcmp(fargv[fargc], "@New_line") == 0)
                {
                    fgets((char *)curr_cmd + curpos, XA_MAX_CMD_LINE_LENGTH, param_file_id);
                    continue;
                }
                curpos += strlen(fargv[fargc]);
                while(*(curr_cmd + curpos)==' ' || *(curr_cmd + curpos)=='\t')
                    curpos++;
                fargc++;
            }

            if(fargc < 1)   // for blank lines etc. 
                continue;

            if(strcmp(fargv[0], "@Input_path") == 0)
            {
                if(fargc > 1) strcpy((char *)pb_input_file_path, fargv[1]);
                else strcpy((char *)pb_input_file_path, "");
                continue;
            }

            if(strcmp(fargv[0], "@Start") == 0)
            {
                processcmd = 1;
                continue;
            }

            if(strcmp(fargv[0], "@Stop") == 0)
            {
                processcmd = 0;
                continue;
            }

            // otherwise if this a normal command and its enabled for execution 
            if(processcmd)
            {
                int i;

                pargv[0] = argv[0];
                for(i = 0; i < fargc; i++)
                {
                    fprintf(stdout, "%s ", fargv[i]);
                    pargv[i+1] = fargv[i];
                }

                fprintf(stdout, "\n");

                if(err_code == 0)
                    xa_nn_main_process(fargc+1, pargv);

            }
        }
    }
    else
    {
        int i;

        for(i = 1; i < argc; i++)
        {
            fprintf(stdout, "%s ", argv[i]);

        }

        fprintf(stdout, "\n");

        if(err_code == 0)
            xa_nn_main_process(argc, argv);

    }

    return 0;
}

