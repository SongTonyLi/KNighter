### Report Summary

File:| avfilter/vf_ssim360.c  
---|---  
Warning:| line 713, column 20  
division by possibly zero aggregate factor  
  
### Annotated Source Code


597   |                 ss  += b*b;
598   |                 s12 += a*b;
599   |             }
600   |         }
601   |  
602   |         sums[z][0] = s1;
603   |         sums[z][1] = s2;
604   |         sums[z][2] = ss;
605   |         sums[z][3] = s12;
606   |  
607   |         offset_x += 4;
608   |     }
609   | }
610   |  
611   | static float get_radius_between_negative_and_positive_pi(float theta)
612   | {
613   |  int floor_theta_by_2pi, floor_theta_by_pi;
614   |  
615   |  // Convert theta to range [0, 2*pi]
616   |     floor_theta_by_2pi = (int)(theta / (2.0f * M_PI_F)) - (theta < 0.0f);
617   |     theta -= 2.0f * M_PI_F * floor_theta_by_2pi;
618   |  
619   |  // Convert theta to range [-pi, pi]
620   |     floor_theta_by_pi = theta / M_PI_F;
621   |     theta -= 2.0f * M_PI_F * floor_theta_by_pi;
622   |  return FFMIN(M_PI_F, FFMAX(-M_PI_F, theta));
623   | }
624   |  
625   | static float get_heat(HeatmapList *heatmaps, float angular_resoluation, float norm_tape_pos)
626   | {
627   |  float pitch, yaw, norm_pitch, norm_yaw;
628   |  int w, h;
629   |  
630   |  if (!heatmaps)
631   |  return 1.0f;
632   |  
633   |     pitch = asinf(norm_tape_pos*2);
634   |     yaw   = M_PI_2_F * pitch / angular_resoluation;
635   |     yaw   = get_radius_between_negative_and_positive_pi(yaw);
636   |  
637   |  // normalize into [0,1]
638   |     norm_pitch = 1.0f - (pitch / M_PI_F + 0.5f);
639   |     norm_yaw   = yaw / 2.0f / M_PI_F + 0.5f;
640   |  
641   |  // get heat on map
642   |     w = FFMIN(heatmaps->map.w - 1, FFMAX(0, heatmaps->map.w * norm_yaw));
643   |     h = FFMIN(heatmaps->map.h - 1, FFMAX(0, heatmaps->map.h * norm_pitch));
644   |  return heatmaps->map.value[h * heatmaps->map.w + w];
645   | }
646   |  
647   | static double
648   | ssim360_tape(uint8_t *main, BilinearMap *main_maps,
649   |              uint8_t *ref, BilinearMap *ref_maps,
650   |  int tape_length, int max_value, void *temp,
651   |  double *ssim360_hist, double *ssim360_hist_net,
652   |  float angular_resolution, HeatmapList *heatmaps)
653   | {
654   |  int horizontal_block_count = 2;
655   |  int vertical_block_count = tape_length >> 2;
656   |  
657   |  int z = 0, y;
658   |  // Since the tape will be very long and we need to average over all 8x8 blocks, use double
659   |  double ssim360 = 0.0;
660   |  double sum_weight = 0.0;
661   |  
662   |  int (*sum0)[4] = temp;
663   |  int (*sum1)[4] = sum0 + horizontal_block_count + 3;
664   |  
665   |  for (y = 1; y < vertical_block_count; y++) {
    12←Assuming 'y' is >= 'vertical_block_count'→
    13←Loop condition is false. Execution continues on line 713→
666   |  int fs1, fs2, fss, fs12, hist_index;
667   |  float norm_tape_pos, weight;
668   |  double sample_ssim360;
669   |  
670   |  for (; z <= y; z++) {
671   |  FFSWAP(void*, sum0, sum1);
672   |             ssim360_4x4x2_tape(main, main_maps, ref, ref_maps, z*4, max_value, sum0);
673   |         }
674   |  
675   |  // Given we have only one 8x8 block, following sums fit within 26 bits even for 10bit videos
676   |         fs1  = sum0[0][0] + sum0[1][0] + sum1[0][0] + sum1[1][0];
677   |         fs2  = sum0[0][1] + sum0[1][1] + sum1[0][1] + sum1[1][1];
678   |         fss  = sum0[0][2] + sum0[1][2] + sum1[0][2] + sum1[1][2];
679   |         fs12 = sum0[0][3] + sum0[1][3] + sum1[0][3] + sum1[1][3];
680   |  
681   |  if (max_value > 255) {
682   |  // Since we need high precision to multiply fss / fs12 by 64, use double
683   |  double ssim_c1_d = .01*.01*64*max_value*max_value;
684   |  double ssim_c2_d = .03*.03*64*63*max_value*max_value;
685   |  
686   |  double vars = 64. * fss - 1. * fs1 * fs1 - 1. * fs2 * fs2;
687   |  double covar = 64. * fs12 - 1.*fs1 * fs2;
688   |             sample_ssim360 = (2. * fs1 * fs2 + ssim_c1_d) * (2. * covar + ssim_c2_d)
689   |                         / ((1. * fs1 * fs1 + 1. * fs2 * fs2 + ssim_c1_d) * (1. * vars + ssim_c2_d));
690   |         } else {
691   |  static const int ssim_c1 = (int)(.01*.01*255*255*64 + .5);
692   |  static const int ssim_c2 = (int)(.03*.03*255*255*64*63 + .5);
693   |  
694   |  int vars = fss * 64 - fs1 * fs1 - fs2 * fs2;
695   |  int covar = fs12 * 64 - fs1 * fs2;
696   |             sample_ssim360 = (double)(2 * fs1 * fs2 + ssim_c1) * (double)(2 * covar + ssim_c2)
697   |                         / ((double)(fs1 * fs1 + fs2 * fs2 + ssim_c1) * (double)(vars + ssim_c2));
698   |         }
699   |  
700   |         hist_index = (int)(sample_ssim360 * ((double)SSIM360_HIST_SIZE - .5));
701   |         hist_index = av_clip(hist_index, 0, SSIM360_HIST_SIZE - 1);
702   |  
703   |         norm_tape_pos = (y - 0.5f) / (vertical_block_count - 1.0f) - 0.5f;
704   |  // weight from an input heatmap if available, otherwise weight = 1.0
705   |         weight = get_heat(heatmaps, angular_resolution, norm_tape_pos);
706   |         ssim360_hist[hist_index] += weight;
707   |         *ssim360_hist_net += weight;
708   |  
709   |         ssim360 += (sample_ssim360 * weight);
710   |         sum_weight += weight;
711   |     }
712   |  
713   |  return ssim360 / sum_weight;
    14←division by possibly zero aggregate factor
714   | }
715   |  
716   | static void compute_bilinear_map(SampleParams *p, BilinearMap *m, float x, float y)
717   | {
718   |  float fixed_point_scale = (float)(1 << FIXED_POINT_PRECISION);
719   |  
720   |  // All operations in here will fit in the 22 bit mantissa of floating point,
721   |  // since the fixed point precision is well under 22 bits
722   |  float x_image = av_clipf(x * p->x_image_range, 0, p->x_image_range) + p->x_image_offset;
723   |  float y_image = av_clipf(y * p->y_image_range, 0, p->y_image_range) + p->y_image_offset;
724   |  
725   |  int x_floor = x_image;
726   |  int y_floor = y_image;
727   |  float x_diff = x_image - x_floor;
728   |  float y_diff = y_image - y_floor;
729   |  
730   |  int x_ceil = x_floor + (x_diff > 1e-6);
731   |  int y_ceil = y_floor + (y_diff > 1e-6);
732   |  float x_inv_diff = 1.0f - x_diff;
733   |  float y_inv_diff = 1.0f - y_diff;
734   |  
735   |  // Indices of the 4 samples from source frame
736   |     m->tli = x_floor    + y_floor   * p->stride;
737   |     m->tri = x_ceil     + y_floor   * p->stride;
738   |     m->bli = x_floor    + y_ceil    * p->stride;
739   |     m->bri = x_ceil     + y_ceil    * p->stride;
740   |  
741   |  // Scale to be applied to each of the 4 samples from source frame
742   |     m->tlf = x_inv_diff * y_inv_diff * fixed_point_scale;
743   |     m->trf = x_diff     * y_inv_diff * fixed_point_scale;
1078  |  for (int i = 0; i < s->nb_components; i ++) {
1079  |  int ref_width = s->ref_planewidth[i];
1080  |  int ref_height = s->ref_planeheight[i];
1081  |  int main_width = s->main_planewidth[i];
1082  |  int main_height = s->main_planeheight[i];
1083  |  
1084  |  int is_ref_LR = (ref_stereo_format == STEREO_FORMAT_LR);
1085  |  int is_ref_TB = (ref_stereo_format == STEREO_FORMAT_TB);
1086  |  int is_main_LR = (main_stereo_format == STEREO_FORMAT_LR);
1087  |  int is_main_TB = (main_stereo_format == STEREO_FORMAT_TB);
1088  |  
1089  |  int ref_image_width = is_ref_LR ? ref_width >> 1 : ref_width;
1090  |  int ref_image_height = is_ref_TB ? ref_height >> 1 : ref_height;
1091  |  int main_image_width = is_main_LR ? main_width >> 1 : main_width;
1092  |  int main_image_height = is_main_TB ? main_height >> 1 : main_height;
1093  |  
1094  |  for (int eye = 0; eye < min_eye_count; eye ++) {
1095  |             SampleParams ref_sample_params = {
1096  |                 .stride         = ref->linesize[i],
1097  |                 .planewidth     = ref_width,
1098  |                 .planeheight    = ref_height,
1099  |                 .x_image_range  = ref_image_width - 1,
1100  |                 .y_image_range  = ref_image_height - 1,
1101  |                 .x_image_offset = is_ref_LR * eye * ref_image_width,
1102  |                 .y_image_offset = is_ref_TB * eye * ref_image_height,
1103  |                 .projection     = s->ref_projection,
1104  |                 .expand_coef    = 1.f + s->ref_pad,
1105  |             };
1106  |  
1107  |             SampleParams main_sample_params = {
1108  |                 .stride         = main->linesize[i],
1109  |                 .planewidth     = main_width,
1110  |                 .planeheight    = main_height,
1111  |                 .x_image_range  = main_image_width - 1,
1112  |                 .y_image_range  = main_image_height - 1,
1113  |                 .x_image_offset = is_main_LR * eye * main_image_width,
1114  |                 .y_image_offset = is_main_TB * eye * main_image_height,
1115  |                 .projection     = s->main_projection,
1116  |                 .expand_coef    = 1.f + s->main_pad,
1117  |             };
1118  |  
1119  |             ret = generate_eye_tape_map(s, i, eye, &ref_sample_params, &main_sample_params);
1120  |  if (ret < 0)
1121  |  return ret;
1122  |         }
1123  |     }
1124  |  
1125  |  return 0;
1126  | }
1127  |  
1128  | static int do_ssim360(FFFrameSync *fs)
1129  | {
1130  |  AVFilterContext *ctx = fs->parent;
1131  |     SSIM360Context *s = ctx->priv;
1132  |     AVFrame *master, *ref;
1133  |     AVDictionary **metadata;
1134  |  double c[4], ssim360v = 0.0, ssim360p50 = 0.0;
1135  |  int ret;
1136  |  int need_frame_skip = s->nb_net_frames % (s->frame_skip_ratio + 1);
1137  |     HeatmapList* h_ptr = NULL;
1138  |  
1139  |     ret = ff_framesync_dualinput_get(fs, &master, &ref);
1140  |  if (ret < 0)
    1Assuming 'ret' is >= 0→
    2←Taking false branch→
1141  |  return ret;
1142  |  
1143  |  s->nb_net_frames++;
1144  |  
1145  |  if (need_frame_skip)
    3←Assuming 'need_frame_skip' is 0→
    4←Taking false branch→
1146  |  return ff_filter_frame(ctx->outputs[0], master);
1147  |  
1148  |  metadata = &master->metadata;
1149  |  
1150  |  if (s->use_tape && !s->tape_length[0]) {
    5←Assuming field 'use_tape' is not equal to 0→
    6←Assuming the condition is false→
    7←Taking false branch→
1151  |         ret = generate_tape_maps(s, master, ref);
1152  |  if (ret < 0)
1153  |  return ret;
1154  |     }
1155  |  
1156  |  for (int i = 0; i < s->nb_components; i++) {
    8←Assuming 'i' is < field 'nb_components'→
    9←Loop condition is true.  Entering loop body→
1157  |  if (s->use_tape9.1Field 'use_tape' is not equal to 0) {
    10←Taking true branch→
1158  |  c[i] = ssim360_tape(master->data[i], s->main_tape_map[i][0],
    11←Calling 'ssim360_tape'→
1159  |  ref->data[i],    s->ref_tape_map [i][0],
1160  |  s->tape_length[i], s->max, s->temp,
1161  |  s->ssim360_hist[i], &s->ssim360_hist_net[i],
1162  |  s->angular_resolution[i][0], s->heatmaps);
1163  |  
1164  |  if (s->ref_tape_map[i][1]) {
1165  |                 c[i] += ssim360_tape(master->data[i], s->main_tape_map[i][1],
1166  |                                      ref->data[i],    s->ref_tape_map[i][1],
1167  |                                      s->tape_length[i], s->max, s->temp,
1168  |                                      s->ssim360_hist[i], &s->ssim360_hist_net[i],
1169  |                                      s->angular_resolution[i][1], s->heatmaps);
1170  |                 c[i] /= 2.f;
1171  |             }
1172  |         } else {
1173  |             c[i] = s->ssim360_plane(master->data[i], master->linesize[i],
1174  |                                     ref->data[i],    ref->linesize[i],
1175  |                                     s->ref_planewidth[i], s->ref_planeheight[i],
1176  |                                     s->temp, s->max, s->density);
1177  |         }
1178  |  
1179  |         s->ssim360[i] += c[i];
1180  |         ssim360v      += s->coefs[i] * c[i];
1181  |     }
1182  |  
1183  |     s->nb_ssim_frames++;
1184  |  if (s->heatmaps) {
1185  |         map_uninit(&s->heatmaps->map);
1186  |         h_ptr = s->heatmaps;
1187  |         s->heatmaps = s->heatmaps->next;
1188  |         av_freep(&h_ptr);
1189  |     }
1190  |     s->ssim360_total += ssim360v;
1191  |  
1192  |  // Record percentiles from histogram and attach metadata when using tape