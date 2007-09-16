/*
   Copyright (C) 2007 Amaury Hazan <ahazan@iua.upf.edu>
                  and Paul Brossier <piem@piem.org>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/


#include "aubio_priv.h"
#include "sample.h"
#include "filterbank.h"

#include "stdio.h"

#define USE_EQUAL_GAIN 1
#define VERY_SMALL_NUMBER 2e-42

/** \brief A structure to store a set of n_filters filters of lenghts win_s */
struct aubio_filterbank_t_ {
    uint_t win_s;
    uint_t n_filters;
    fvec_t **filters;
};

aubio_filterbank_t * new_aubio_filterbank(uint_t n_filters, uint_t win_s){
  /** allocating space for filterbank object */
  aubio_filterbank_t * fb = AUBIO_NEW(aubio_filterbank_t);
  uint_t filter_cnt;
  fb->win_s=win_s;
  fb->n_filters=n_filters;

  /** allocating filter tables */
  fb->filters=AUBIO_ARRAY(fvec_t*,n_filters);
  for (filter_cnt=0; filter_cnt<n_filters; filter_cnt++)
    /* considering one-channel filters */
    fb->filters[filter_cnt]=new_fvec(win_s, 1);

  return fb;
}

aubio_filterbank_t * new_aubio_filterbank_mfcc(uint_t n_filters, uint_t win_s, uint_t samplerate, smpl_t freq_min, smpl_t freq_max){
  
  smpl_t nyquist = samplerate/2.;
  uint_t style = 1;
  aubio_filterbank_t * fb = new_aubio_filterbank(n_filters, win_s);

  uint_t n, i, k, *fft_peak, M, next_peak; 
  smpl_t norm, mel_freq_max, mel_freq_min, norm_fact, height, inc, val, 
         freq_bw_mel, *mel_peak, *height_norm, *lin_peak;

  mel_peak = height_norm = lin_peak = NULL;
  fft_peak = NULL;
  norm = 1; 

  mel_freq_max = 1127 * log(1 + freq_max / 700);
  mel_freq_min = 1127 * log(1 + freq_min / 700);
  freq_bw_mel = (mel_freq_max - mel_freq_min) / fb->n_filters;

  mel_peak = (smpl_t *)malloc((fb->n_filters + 2) * sizeof(smpl_t)); 
  /* +2 for zeros at start and end */
  lin_peak = (smpl_t *)malloc((fb->n_filters + 2) * sizeof(smpl_t));
  fft_peak = (uint_t *)malloc((fb->n_filters + 2) * sizeof(uint_t));
  height_norm = (smpl_t *)malloc(fb->n_filters * sizeof(smpl_t));

  if(mel_peak == NULL || height_norm == NULL || 
      lin_peak == NULL || fft_peak == NULL)
    return NULL;

  M = fb->win_s >> 1;

  mel_peak[0] = mel_freq_min;
  lin_peak[0] = 700 * (exp(mel_peak[0] / 1127) - 1);
  fft_peak[0] = lin_peak[0] / nyquist * M;

  for (n = 1; n <= fb->n_filters; n++){  
    /*roll out peak locations - mel, linear and linear on fft window scale */
    mel_peak[n] = mel_peak[n - 1] + freq_bw_mel;
    lin_peak[n] = 700 * (exp(mel_peak[n] / 1127) -1);
    fft_peak[n] = lin_peak[n] / nyquist * M;
  }

  for (n = 0; n < fb->n_filters; n++){
    /*roll out normalised gain of each peak*/
    if (style == USE_EQUAL_GAIN){
      height = 1; 
      norm_fact = norm;
    }
    else{
      height = 2 / (lin_peak[n + 2] - lin_peak[n]);
      norm_fact = norm / (2 / (lin_peak[2] - lin_peak[0]));
    }
    height_norm[n] = height * norm_fact;
  }

  i = 0;

  for(n = 0; n < fb->n_filters; n++){

    /*calculate the rise increment*/
    if(n > 0)
      inc = height_norm[n] / (fft_peak[n] - fft_peak[n - 1]);
    else
      inc = height_norm[n] / fft_peak[n];
    val = 0;  

    /*zero the start of the array*/
    for(k = 0; k < i; k++)
      fb->filters[n]->data[0][k]=0.f;

    /*fill in the rise */
    for(; i <= fft_peak[n]; i++){ 
      fb->filters[n]->data[0][k]=val;
      val += inc;
    }

    /*calculate the fall increment */
    inc = height_norm[n] / (fft_peak[n + 1] - fft_peak[n]);

    val = 0;
    next_peak = fft_peak[n + 1];

    /*reverse fill the 'fall' */
    for(i = next_peak; i > fft_peak[n]; i--){ 
      fb->filters[n]->data[0][k]=val;
      val += inc;
    }

    /*zero the rest of the array*/
    for(k = next_peak + 1; k < fb->win_s; k++)
      fb->filters[n]->data[0][k]=0.f;


  }

  free(mel_peak);
  free(lin_peak);
  free(height_norm);
  free(fft_peak);


  return fb;

}

/*
FB initialization based on Slaney's auditory toolbox
TODO:
  *solve memory leak problems while
  *solve quantization issues when constructing signal:
    *bug for win_s=512
    *corrections for win_s=1024 -> why even filters with smaller amplitude

*/

aubio_filterbank_t * new_aubio_filterbank_mfcc2(uint_t n_filters, uint_t win_s, uint_t samplerate, smpl_t freq_min, smpl_t freq_max){
  
  aubio_filterbank_t * fb = new_aubio_filterbank(n_filters, win_s);
  
  
  //slaney params
  smpl_t lowestFrequency = 133.3333;
  smpl_t linearSpacing = 66.66666666;
  smpl_t logSpacing = 1.0711703;

  uint_t linearFilters = 13;
  uint_t logFilters = 27;
  uint_t allFilters = linearFilters + logFilters;
  
  //buffers for computing filter frequencies
  fvec_t * freqs=new_fvec(allFilters+2 , 1);
  
  fvec_t * lower_freqs=new_fvec( allFilters, 1);
  fvec_t * upper_freqs=new_fvec( allFilters, 1);
  fvec_t * center_freqs=new_fvec( allFilters, 1);

  
  fvec_t * triangle_heights=new_fvec( allFilters, 1);
  //lookup table of each bin frequency in hz
  fvec_t * fft_freqs=new_fvec(win_s, 1);

  uint_t filter_cnt, bin_cnt;
  
  //first step: filling all the linear filter frequencies
  for(filter_cnt=0; filter_cnt<linearFilters; filter_cnt++){
    freqs->data[0][filter_cnt]=lowestFrequency+ filter_cnt*linearSpacing;
  }
  smpl_t lastlinearCF=freqs->data[0][filter_cnt-1];
  
  //second step: filling all the log filter frequencies
  for(filter_cnt=0; filter_cnt<logFilters+2; filter_cnt++){
    freqs->data[0][filter_cnt+linearFilters]=lastlinearCF*(pow(logSpacing,filter_cnt+1));
  }

  //Option 1. copying interesting values to lower_freqs, center_freqs and upper freqs arrays
  //TODO: would be nicer to have a reference to freqs->data, anyway we do not care in this init step
    
  for(filter_cnt=0; filter_cnt<allFilters; filter_cnt++){
    lower_freqs->data[0][filter_cnt]=freqs->data[0][filter_cnt];
    center_freqs->data[0][filter_cnt]=freqs->data[0][filter_cnt+1];
    upper_freqs->data[0][filter_cnt]=freqs->data[0][filter_cnt+2];
  }


  //computing triangle heights so that each triangle has unit area
  for(filter_cnt=0; filter_cnt<allFilters; filter_cnt++){
    triangle_heights->data[0][filter_cnt]=2./(upper_freqs->data[0][filter_cnt]-lower_freqs->data[0][filter_cnt]);
  }
  
  
  //AUBIO_DBG
  AUBIO_DBG("filter tables frequencies\n");
  for(filter_cnt=0; filter_cnt<allFilters; filter_cnt++)
    AUBIO_DBG("filter n. %d %f %f %f %f\n",filter_cnt, lower_freqs->data[0][filter_cnt], center_freqs->data[0][filter_cnt], upper_freqs->data[0][filter_cnt], triangle_heights->data[0][filter_cnt]);

  //filling the fft_freqs lookup table, which assigns the frequency in hz to each bin
  for(bin_cnt=0; bin_cnt<win_s; bin_cnt++){
    //TODO: check the formula!
    fft_freqs->data[0][bin_cnt]= (smpl_t)samplerate* (smpl_t)bin_cnt/ (smpl_t)win_s;
  }

  //building each filter table
  for(filter_cnt=0; filter_cnt<allFilters; filter_cnt++){

    //TODO:check special case : lower freq =0
    //calculating rise increment in mag/Hz
    smpl_t riseInc= triangle_heights->data[0][filter_cnt]/(center_freqs->data[0][filter_cnt]-lower_freqs->data[0][filter_cnt]);
    

    AUBIO_DBG("\nfilter %d",filter_cnt);
    //zeroing begining of filter
    AUBIO_DBG("\nzero begin\n");
    for(bin_cnt=0; bin_cnt<win_s-1; bin_cnt++){
      //zeroing beigining of array
      fb->filters[filter_cnt]->data[0][bin_cnt]=0.f;
      AUBIO_DBG(".");
      //AUBIO_DBG("%f %f %f\n", fft_freqs->data[0][bin_cnt], fft_freqs->data[0][bin_cnt+1], lower_freqs->data[0][filter_cnt]); 
      if(fft_freqs->data[0][bin_cnt]<= lower_freqs->data[0][filter_cnt] && fft_freqs->data[0][bin_cnt+1]> lower_freqs->data[0][filter_cnt]){
        break;
      }
    }
    bin_cnt++;
    
    AUBIO_DBG("\npos slope\n");
    //positive slope
    for(; bin_cnt<win_s-1; bin_cnt++){
      AUBIO_DBG(".");
      fb->filters[filter_cnt]->data[0][bin_cnt]=(fft_freqs->data[0][bin_cnt]-lower_freqs->data[0][filter_cnt])*riseInc;
      //if(fft_freqs->data[0][bin_cnt]<= center_freqs->data[0][filter_cnt] && fft_freqs->data[0][bin_cnt+1]> center_freqs->data[0][filter_cnt])
      if(fft_freqs->data[0][bin_cnt+1]> center_freqs->data[0][filter_cnt])
        break;
    }
    //bin_cnt++;
    
    
    //negative slope
    AUBIO_DBG("\nneg slope\n");   
    for(; bin_cnt<win_s-1; bin_cnt++){
      //AUBIO_DBG(".");
      
      //checking whether last value is less than 0...
      smpl_t val=triangle_heights->data[0][filter_cnt]-(fft_freqs->data[0][bin_cnt]-center_freqs->data[0][filter_cnt])*riseInc;
      if(val>=0)
        fb->filters[filter_cnt]->data[0][bin_cnt]=val;
      else fb->filters[filter_cnt]->data[0][bin_cnt]=0.f;
      
      //if(fft_freqs->data[0][bin_cnt]<= upper_freqs->data[0][bin_cnt] && fft_freqs->data[0][bin_cnt+1]> upper_freqs->data[0][filter_cnt])
      //TODO: CHECK whether bugfix correct
      if(fft_freqs->data[0][bin_cnt+1]> upper_freqs->data[0][filter_cnt])
        break;
    }
    //bin_cnt++;
    
    AUBIO_DBG("\nzero end\n");
    //zeroing tail
    for(; bin_cnt<win_s; bin_cnt++)
      //AUBIO_DBG(".");
      fb->filters[filter_cnt]->data[0][bin_cnt]=0.f;

  }
  
  

  del_fvec(freqs);
  del_fvec(lower_freqs);
  del_fvec(upper_freqs);
  del_fvec(center_freqs);

  del_fvec(triangle_heights);
  del_fvec(fft_freqs);

  return fb;

}

void aubio_dump_filterbank(aubio_filterbank_t * fb){

  FILE * mlog;
  mlog=fopen("filterbank.txt","w");
  
  int k,n;
  //dumping filter values
  //smpl_t area_tmp=0.f;
  for(n = 0; n < fb->n_filters; n++){
    for(k = 0; k < fb->win_s; k++){
      fprintf(mlog,"%f ",fb->filters[n]->data[0][k]);
    }
    fprintf(mlog,"\n");
  }
  
  if(mlog) fclose(mlog);
}

void del_aubio_filterbank(aubio_filterbank_t * fb){
  uint_t filter_cnt;
  /** deleting filter tables first */
  for (filter_cnt=0; filter_cnt<fb->n_filters; filter_cnt++)
    del_fvec(fb->filters[filter_cnt]);
  AUBIO_FREE(fb->filters);
  AUBIO_FREE(fb);
}

void aubio_filterbank_do(aubio_filterbank_t * f, cvec_t * in, fvec_t *out) {
  uint_t n, filter_cnt;
  for(filter_cnt = 0; (filter_cnt < f->n_filters)
    && (filter_cnt < out->length); filter_cnt++){
      out->data[0][filter_cnt] = 0.f;
      for(n = 0; n < in->length; n++){
          out->data[0][filter_cnt] += in->norm[0][n] 
            * f->filters[filter_cnt]->data[0][n];
      }
      out->data[0][filter_cnt] =
        LOG(out->data[0][filter_cnt] < VERY_SMALL_NUMBER ? 
            VERY_SMALL_NUMBER : out->data[0][filter_cnt]);
  }

  return;
}
