# ffmpeg - scan-build results

User:| cc@mingkai-gigaio  
---|---  
Working Directory:| /home/cc/ai2sec/KNighter/ffmpeg  
Command Line:| make -j32  
Clang Version:| clang version 18.1.8 (https://github.com/ise-uiuc/KNighter.git
f4e834b3074104001c31c5c262ab11e28d1a8e38)  
Date:| Thu Apr 30 06:22:28 2026  
  
## Bug Summary

Bug Type| Quantity| Display?  
---|---|---  
All Bugs| 44|  
Possible division by zero|  
Possible division by zero| 44|  
  
## Reports

Bug Group | Bug Type ▾ | File | Function/Method | Line | Path Length |   
---|---|---|---|---|---|---  
Possible division by zero| Possible division by zero|
avfilter/af_aspectralstats.c| spectral_spread| 283| 21| [View
Report](report-04cf79.html#EndPath)  
Possible division by zero| Possible division by zero| avfilter/af_alimiter.c|
filter_frame| 181| 11| [View Report](report-085153.html#EndPath)  
Possible division by zero| Possible division by zero|
avfilter/avf_avectorscope.c| filter_frame| 358| 15| [View
Report](report-0aa16d.html#EndPath)  
Possible division by zero| Possible division by zero| avfilter/af_anlmdn.c|
filter_channel| 260| 9| [View Report](report-0c3687.html#EndPath)  
Possible division by zero| Possible division by zero| avcodec/ratecontrol.c|
init_pass2| 432| 28| [View Report](report-16377f.html#EndPath)  
Possible division by zero| Possible division by zero| avcodec/ratecontrol.c|
adaptive_quantization| 858| 34| [View Report](report-22548f.html#EndPath)  
Possible division by zero| Possible division by zero|
avfilter/af_aspectralstats.c| spectral_flatness| 352| 29| [View
Report](report-2b6677.html#EndPath)  
Possible division by zero| Possible division by zero| avfilter/perlin.c|
ff_perlin_get| 222| 3| [View Report](report-2d717c.html#EndPath)  
Possible division by zero| Possible division by zero|
avfilter/vf_readeia608.c| extract_line| 395| 16| [View
Report](report-334cd8.html#EndPath)  
Possible division by zero| Possible division by zero| avfilter/vf_vif.c|
compute_vif2| 389| 17| [View Report](report-429959.html#EndPath)  
Possible division by zero| Possible division by zero| avfilter/af_alimiter.c|
filter_frame| 203| 19| [View Report](report-44053e.html#EndPath)  
Possible division by zero| Possible division by zero| avcodec/qdm2.c|
synthfilt_build_sb_samples| 833| 36| [View Report](report-4c1d73.html#EndPath)  
Possible division by zero| Possible division by zero| swresample/resample.c|
build_filter| 118| 41| [View Report](report-4eeafa.html#EndPath)  
Possible division by zero| Possible division by zero| avcodec/ratecontrol.c|
adaptive_quantization| 883| 81| [View Report](report-51daca.html#EndPath)  
Possible division by zero| Possible division by zero|
avfilter/afir_template.c| ir_gain_float| 67| 19| [View
Report](report-5f6f5e.html#EndPath)  
Possible division by zero| Possible division by zero| swresample/resample.c|
build_filter| 104| 41| [View Report](report-6a2177.html#EndPath)  
Possible division by zero| Possible division by zero| avfilter/vf_ssim360.c|
ssim360_tape| 713| 15| [View Report](report-6d5fdc.html#EndPath)  
Possible division by zero| Possible division by zero| avfilter/vf_bm3d.c|
final_block_filtering| 598| 11| [View Report](report-6e0f3b.html#EndPath)  
Possible division by zero| Possible division by zero| avfilter/vf_bm3d.c|
do_output16| 678| 7| [View Report](report-6e7526.html#EndPath)  
Possible division by zero| Possible division by zero| avfilter/af_aemphasis.c|
set_highshelf_rbj| 170| 20| [View Report](report-7e1149.html#EndPath)  
Possible division by zero| Possible division by zero| avfilter/vf_ssim360.c|
ssim360_plane_8bit| 538| 3| [View Report](report-7f8fd2.html#EndPath)  
Possible division by zero| Possible division by zero| avfilter/af_alimiter.c|
filter_frame| 203| 19| [View Report](report-809268.html#EndPath)  
Possible division by zero| Possible division by zero|
avfilter/vf_readeia608.c| extract_line| 396| 17| [View
Report](report-8444c8.html#EndPath)  
Possible division by zero| Possible division by zero| avfilter/af_afftdn.c|
process_frame| 462| 29| [View Report](report-8655bf.html#EndPath)  
Possible division by zero| Possible division by zero| avfilter/vf_convolve.c|
prepare_impulse| 631| 7| [View Report](report-88cfc4.html#EndPath)  
Possible division by zero| Possible division by zero| avcodec/aaccoder.c|
search_for_pns| 617| 32| [View Report](report-8e1c98.html#EndPath)  
Possible division by zero| Possible division by zero|
avfilter/af_aspectralstats.c| spectral_centroid| 268| 19| [View
Report](report-8e62db.html#EndPath)  
Possible division by zero| Possible division by zero|
avfilter/af_aspectralstats.c| spectral_skewness| 304| 23| [View
Report](report-98c863.html#EndPath)  
Possible division by zero| Possible division by zero| avfilter/af_alimiter.c|
filter_frame| 261| 20| [View Report](report-9d39cf.html#EndPath)  
Possible division by zero| Possible division by zero| avfilter/af_alimiter.c|
filter_frame| 252| 29| [View Report](report-a66d98.html#EndPath)  
Possible division by zero| Possible division by zero|
avfilter/af_dynaudnorm.c| init_gaussian_filter| 299| 8| [View
Report](report-a92401.html#EndPath)  
Possible division by zero| Possible division by zero| avfilter/vf_ssim360.c|
ssim360_plane_16bit| 506| 3| [View Report](report-aadcb0.html#EndPath)  
Possible division by zero| Possible division by zero| swresample/resample.c|
build_filter| 111| 41| [View Report](report-ac678c.html#EndPath)  
Possible division by zero| Possible division by zero|
avfilter/af_aspectralstats.c| spectral_crest| 367| 31| [View
Report](report-b1f42a.html#EndPath)  
Possible division by zero| Possible division by zero|
avfilter/af_aspectralstats.c| spectral_entropy| 334| 27| [View
Report](report-b73c06.html#EndPath)  
Possible division by zero| Possible division by zero|
avfilter/af_aspectralstats.c| spectral_kurtosis| 320| 25| [View
Report](report-b9736d.html#EndPath)  
Possible division by zero| Possible division by zero| avfilter/vf_bm3d.c|
do_output| 651| 7| [View Report](report-cdd85b.html#EndPath)  
Possible division by zero| Possible division by zero|
avfilter/afir_template.c| ir_gain_double| 67| 19| [View
Report](report-d39166.html#EndPath)  
Possible division by zero| Possible division by zero|
avfilter/af_aspectralstats.c| spectral_slope| 397| 37| [View
Report](report-e1f669.html#EndPath)  
Possible division by zero| Possible division by zero| avfilter/af_anlmdn.c|
filter_channel| 259| 9| [View Report](report-e376d7.html#EndPath)  
Possible division by zero| Possible division by zero| avfilter/af_alimiter.c|
filter_frame| 184| 13| [View Report](report-e75414.html#EndPath)  
Possible division by zero| Possible division by zero| swresample/resample.c|
build_filter| 125| 41| [View Report](report-eae2d8.html#EndPath)  
Possible division by zero| Possible division by zero|
avfilter/af_aspectralstats.c| spectral_decrease| 411| 37| [View
Report](report-f5e4b1.html#EndPath)  
Possible division by zero| Possible division by zero| avfilter/af_aiir.c|
normalize_coeffs| 546| 47| [View Report](report-ff9735.html#EndPath)

