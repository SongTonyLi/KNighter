### Report Summary

File:| avcodec/ratecontrol.c  
---|---  
Warning:| line 858, column 33  
division by possibly zero aggregate factor  
  
### Annotated Source Code


9     |  * modify it under the terms of the GNU Lesser General Public
10    |  * License as published by the Free Software Foundation; either
11    |  * version 2.1 of the License, or (at your option) any later version.
12    |  *
13    |  * FFmpeg is distributed in the hope that it will be useful,
14    |  * but WITHOUT ANY WARRANTY; without even the implied warranty of
15    |  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
16    |  * Lesser General Public License for more details.
17    |  *
18    |  * You should have received a copy of the GNU Lesser General Public
19    |  * License along with FFmpeg; if not, write to the Free Software
20    |  * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
21    |  */
22    |  
23    | /**
24    |  * @file
25    |  * Rate control for video encoders.
26    |  */
27    |  
28    | #include "libavutil/attributes.h"
29    | #include "libavutil/internal.h"
30    | #include "libavutil/mem.h"
31    |  
32    | #include "avcodec.h"
33    | #include "ratecontrol.h"
34    | #include "mpegvideoenc.h"
35    | #include "libavutil/eval.h"
36    |  
37    | void ff_write_pass1_stats(MPVMainEncContext *const m)
38    | {
39    |  const MPVEncContext *const s = &m->s;
40    |     snprintf(s->c.avctx->stats_out, 256,
41    |  "in:%d out:%d type:%d q:%d itex:%d ptex:%d mv:%d misc:%d "
42    |  "fcode:%d bcode:%d mc-var:%"PRId64" var:%"PRId64" icount:%d hbits:%d;\n",
43    |              s->c.cur_pic.ptr->display_picture_number,
44    |              s->c.cur_pic.ptr->coded_picture_number,
45    |              s->c.pict_type,
46    |              s->c.cur_pic.ptr->f->quality,
47    |              s->i_tex_bits,
48    |              s->p_tex_bits,
49    |              s->mv_bits,
50    |              s->misc_bits,
51    |              s->f_code,
52    |              s->b_code,
53    |              m->mc_mb_var_sum,
54    |              m->mb_var_sum,
55    |              s->i_count,
56    |              m->header_bits);
57    | }
58    |  
59    | static AVRational get_fpsQ(AVCodecContext *avctx)
60    | {
61    |  if (avctx->framerate.num > 0 && avctx->framerate.den > 0)
62    |  return avctx->framerate;
63    |  
64    |  return av_inv_q(avctx->time_base);
65    | }
66    |  
67    | static double get_fps(AVCodecContext *avctx)
68    | {
69    |  return av_q2d(get_fpsQ(avctx));
70    | }
71    |  
72    | static inline double qp2bits(const RateControlEntry *rce, double qp)
73    | {
74    |  if (qp <= 0.0) {
75    |         av_log(NULL, AV_LOG_ERROR, "qp<=0.0\n");
76    |     }
77    |  return rce->qscale * (double)(rce->i_tex_bits + rce->p_tex_bits + 1) / qp;
78    | }
79    |  
80    | static double qp2bits_cb(void *rce, double qp)
81    | {
82    |  return qp2bits(rce, qp);
83    | }
84    |  
85    | static inline double bits2qp(const RateControlEntry *rce, double bits)
86    | {
87    |  if (bits < 0.9) {
88    |         av_log(NULL, AV_LOG_ERROR, "bits<0.9\n");
89    |     }
90    |  return rce->qscale * (double)(rce->i_tex_bits + rce->p_tex_bits + 1) / bits;
91    | }
92    |  
93    | static double bits2qp_cb(void *rce, double qp)
94    | {
95    |  return bits2qp(rce, qp);
96    | }
97    |  
98    | static double get_diff_limited_q(MPVMainEncContext *m, const RateControlEntry *rce, double q)
99    | {
100   |     MPVEncContext      *const   s = &m->s;
101   |     RateControlContext *const rcc = &m->rc_context;
102   |     AVCodecContext     *const   a = s->c.avctx;
103   |  const int pict_type       = rce->new_pict_type;
104   |  const double last_p_q     = rcc->last_qscale_for[AV_PICTURE_TYPE_P];
105   |  const double last_non_b_q = rcc->last_qscale_for[rcc->last_non_b_pict_type];
106   |  
107   |  if (pict_type == AV_PICTURE_TYPE_I &&
108   |         (a->i_quant_factor > 0.0 || rcc->last_non_b_pict_type == AV_PICTURE_TYPE_P))
109   |         q = last_p_q * FFABS(a->i_quant_factor) + a->i_quant_offset;
110   |  else if (pict_type == AV_PICTURE_TYPE_B &&
111   |              a->b_quant_factor > 0.0)
112   |         q = last_non_b_q * a->b_quant_factor + a->b_quant_offset;
113   |  if (q < 1)
114   |         q = 1;
115   |  
116   |  /* last qscale / qdiff stuff */
117   |  if (rcc->last_non_b_pict_type == pict_type || pict_type != AV_PICTURE_TYPE_I) {
118   |  double last_q     = rcc->last_qscale_for[pict_type];
119   |  const int maxdiff = FF_QP2LAMBDA * a->max_qdiff;
120   |  
121   |  if (q > last_q + maxdiff)
122   |             q = last_q + maxdiff;
123   |  else if (q < last_q - maxdiff)
124   |             q = last_q - maxdiff;
125   |     }
126   |  
127   |     rcc->last_qscale_for[pict_type] = q; // Note we cannot do that after blurring
128   |  
129   |  if (pict_type != AV_PICTURE_TYPE_B)
130   |         rcc->last_non_b_pict_type = pict_type;
131   |  
132   |  return q;
133   | }
134   |  
135   | /**
136   |  * Get the qmin & qmax for pict_type.
137   |  */
138   | static void get_qminmax(int *qmin_ret, int *qmax_ret, MPVMainEncContext *const m, int pict_type)
139   | {
140   |     MPVEncContext *const s = &m->s;
141   |  int qmin = m->lmin;
142   |  int qmax = m->lmax;
143   |  
144   |  av_assert0(qmin <= qmax);
145   |  
146   |  switch (pict_type) {
147   |  case AV_PICTURE_TYPE_B:
148   |         qmin = (int)(qmin * FFABS(s->c.avctx->b_quant_factor) + s->c.avctx->b_quant_offset + 0.5);
149   |         qmax = (int)(qmax * FFABS(s->c.avctx->b_quant_factor) + s->c.avctx->b_quant_offset + 0.5);
150   |  break;
151   |  case AV_PICTURE_TYPE_I:
152   |         qmin = (int)(qmin * FFABS(s->c.avctx->i_quant_factor) + s->c.avctx->i_quant_offset + 0.5);
153   |         qmax = (int)(qmax * FFABS(s->c.avctx->i_quant_factor) + s->c.avctx->i_quant_offset + 0.5);
154   |  break;
155   |     }
156   |  
157   |     qmin = av_clip(qmin, 1, FF_LAMBDA_MAX);
158   |     qmax = av_clip(qmax, 1, FF_LAMBDA_MAX);
159   |  
160   |  if (qmax < qmin)
161   |         qmax = qmin;
162   |  
163   |     *qmin_ret = qmin;
164   |     *qmax_ret = qmax;
165   | }
166   |  
167   | static double modify_qscale(MPVMainEncContext *const m, const RateControlEntry *rce,
168   |  double q, int frame_num)
169   | {
170   |     MPVEncContext      *const   s = &m->s;
171   |     RateControlContext *const rcc = &m->rc_context;
172   |  const double buffer_size = s->c.avctx->rc_buffer_size;
173   |  const double fps         = get_fps(s->c.avctx);
174   |  const double min_rate    = s->c.avctx->rc_min_rate / fps;
175   |  const double max_rate    = s->c.avctx->rc_max_rate / fps;
176   |  const int pict_type      = rce->new_pict_type;
177   |  int qmin, qmax;
178   |  
179   |     get_qminmax(&qmin, &qmax, m, pict_type);
180   |  
181   |  /* modulation */
182   |  if (rcc->qmod_freq &&
183   |         frame_num % rcc->qmod_freq == 0 &&
184   |         pict_type == AV_PICTURE_TYPE_P)
185   |         q *= rcc->qmod_amp;
186   |  
187   |  /* buffer overflow/underflow protection */
188   |  if (buffer_size) {
189   |  double expected_size = rcc->buffer_index;
190   |  double q_limit;
191   |  
192   |  if (min_rate) {
193   |  double d = 2 * (buffer_size - expected_size) / buffer_size;
194   |  if (d > 1.0)
729   |  buffer_size, rcc->buffer_index, frame_size, min_rate, max_rate);
730   |  
731   |  if (buffer_size) {
732   |  int left;
733   |  
734   |         rcc->buffer_index -= frame_size;
735   |  if (rcc->buffer_index < 0) {
736   |             av_log(avctx, AV_LOG_ERROR, "rc buffer underflow\n");
737   |  if (frame_size > max_rate && s->c.qscale == avctx->qmax) {
738   |                 av_log(avctx, AV_LOG_ERROR, "max bitrate possibly too small or try trellis with large lmax or increase qmax\n");
739   |             }
740   |             rcc->buffer_index = 0;
741   |         }
742   |  
743   |         left = buffer_size - rcc->buffer_index - 1;
744   |         rcc->buffer_index += av_clip(left, min_rate, max_rate);
745   |  
746   |  if (rcc->buffer_index > buffer_size) {
747   |  int stuffing = ceil((rcc->buffer_index - buffer_size) / 8);
748   |  
749   |  if (stuffing < 4 && s->c.codec_id == AV_CODEC_ID_MPEG4)
750   |                 stuffing = 4;
751   |             rcc->buffer_index -= 8 * stuffing;
752   |  
753   |  if (avctx->debug & FF_DEBUG_RC)
754   |                 av_log(avctx, AV_LOG_DEBUG, "stuffing %d bytes\n", stuffing);
755   |  
756   |  return stuffing;
757   |         }
758   |     }
759   |  return 0;
760   | }
761   |  
762   | static double predict_size(Predictor *p, double q, double var)
763   | {
764   |  return p->coeff * var / (q * p->count);
765   | }
766   |  
767   | static void update_predictor(Predictor *p, double q, double var, double size)
768   | {
769   |  double new_coeff = size * q / (var + 1);
770   |  if (var < 10)
771   |  return;
772   |  
773   |     p->count *= p->decay;
774   |     p->coeff *= p->decay;
775   |     p->count++;
776   |     p->coeff += new_coeff;
777   | }
778   |  
779   | static void adaptive_quantization(RateControlContext *const rcc,
780   |                                   MPVMainEncContext *const m, double q)
781   | {
782   |  MPVEncContext *const s = &m->s;
783   |  const float lumi_masking         = s->c.avctx->lumi_masking / (128.0 * 128.0);
784   |  const float dark_masking         = s->c.avctx->dark_masking / (128.0 * 128.0);
785   |  const float temp_cplx_masking    = s->c.avctx->temporal_cplx_masking;
786   |  const float spatial_cplx_masking = s->c.avctx->spatial_cplx_masking;
787   |  const float p_masking            = s->c.avctx->p_masking;
788   |  const float border_masking       = m->border_masking;
789   |  float bits_sum                   = 0.0;
790   |  float cplx_sum                   = 0.0;
791   |  float *cplx_tab                  = rcc->cplx_tab;
792   |  float *bits_tab                  = rcc->bits_tab;
793   |  const int qmin                   = s->c.avctx->mb_lmin;
794   |  const int qmax                   = s->c.avctx->mb_lmax;
795   |  const int mb_width               = s->c.mb_width;
796   |  const int mb_height              = s->c.mb_height;
797   |  
798   |  for (int i = 0; i < s->c.mb_num; i++) {
    28←Assuming 'i' is >= field 'mb_num'→
    29←Loop condition is false. Execution continues on line 857→
799   |  const int mb_xy = s->c.mb_index2xy[i];
800   |  float temp_cplx = sqrt(s->mc_mb_var[mb_xy]); // FIXME merge in pow()
801   |  float spat_cplx = sqrt(s->mb_var[mb_xy]);
802   |  const int lumi  = s->mb_mean[mb_xy];
803   |  float bits, cplx, factor;
804   |  int mb_x = mb_xy % s->c.mb_stride;
805   |  int mb_y = mb_xy / s->c.mb_stride;
806   |  int mb_distance;
807   |  float mb_factor = 0.0;
808   |  if (spat_cplx < 4)
809   |             spat_cplx = 4;              // FIXME fine-tune
810   |  if (temp_cplx < 4)
811   |             temp_cplx = 4;              // FIXME fine-tune
812   |  
813   |  if ((s->mb_type[mb_xy] & CANDIDATE_MB_TYPE_INTRA)) { // FIXME hq mode
814   |             cplx   = spat_cplx;
815   |             factor = 1.0 + p_masking;
816   |         } else {
817   |             cplx   = temp_cplx;
818   |             factor = pow(temp_cplx, -temp_cplx_masking);
819   |         }
820   |         factor *= pow(spat_cplx, -spatial_cplx_masking);
821   |  
822   |  if (lumi > 127)
823   |             factor *= (1.0 - (lumi - 128) * (lumi - 128) * lumi_masking);
824   |  else
825   |             factor *= (1.0 - (lumi - 128) * (lumi - 128) * dark_masking);
826   |  
827   |  if (mb_x < mb_width / 5) {
828   |             mb_distance = mb_width / 5 - mb_x;
829   |             mb_factor   = (float)mb_distance / (float)(mb_width / 5);
830   |         } else if (mb_x > 4 * mb_width / 5) {
831   |             mb_distance = mb_x - 4 * mb_width / 5;
832   |             mb_factor   = (float)mb_distance / (float)(mb_width / 5);
833   |         }
834   |  if (mb_y < mb_height / 5) {
835   |             mb_distance = mb_height / 5 - mb_y;
836   |             mb_factor   = FFMAX(mb_factor,
837   |  (float)mb_distance / (float)(mb_height / 5));
838   |         } else if (mb_y > 4 * mb_height / 5) {
839   |             mb_distance = mb_y - 4 * mb_height / 5;
840   |             mb_factor   = FFMAX(mb_factor,
841   |  (float)mb_distance / (float)(mb_height / 5));
842   |         }
843   |  
844   |         factor *= 1.0 - border_masking * mb_factor;
845   |  
846   |  if (factor < 0.00001)
847   |             factor = 0.00001;
848   |  
849   |         bits        = cplx * factor;
850   |         cplx_sum   += cplx;
851   |         bits_sum   += bits;
852   |         cplx_tab[i] = cplx;
853   |         bits_tab[i] = bits;
854   |     }
855   |  
856   |  /* handle qmin/qmax clipping */
857   |  if (s->mpv_flags & FF_MPV_FLAG_NAQ) {
    30←Assuming the condition is true→
    31←Taking true branch→
858   |  float factor = bits_sum / cplx_sum;
    32←division by possibly zero aggregate factor
859   |  for (int i = 0; i < s->c.mb_num; i++) {
860   |  float newq = q * cplx_tab[i] / bits_tab[i];
861   |             newq *= factor;
862   |  
863   |  if (newq > qmax) {
864   |                 bits_sum -= bits_tab[i];
865   |                 cplx_sum -= cplx_tab[i] * q / qmax;
866   |             } else if (newq < qmin) {
867   |                 bits_sum -= bits_tab[i];
868   |                 cplx_sum -= cplx_tab[i] * q / qmin;
869   |             }
870   |         }
871   |  if (bits_sum < 0.001)
872   |             bits_sum = 0.001;
873   |  if (cplx_sum < 0.001)
874   |             cplx_sum = 0.001;
875   |     }
876   |  
877   |  for (int i = 0; i < s->c.mb_num; i++) {
878   |  const int mb_xy = s->c.mb_index2xy[i];
879   |  float newq      = q * cplx_tab[i] / bits_tab[i];
880   |  int intq;
881   |  
882   |  if (s->mpv_flags & FF_MPV_FLAG_NAQ) {
883   |             newq *= bits_sum / cplx_sum;
884   |         }
885   |  
886   |         intq = (int)(newq + 0.5);
887   |  
888   |  if (intq > qmax)
889   |             intq = qmax;
890   |  else if (intq < qmin)
891   |             intq = qmin;
892   |         s->lambda_table[mb_xy] = intq;
893   |     }
894   | }
895   |  
896   | void ff_get_2pass_fcode(MPVMainEncContext *const m)
897   | {
898   |     MPVEncContext *const s = &m->s;
899   |  const RateControlContext *rcc = &m->rc_context;
900   |  const RateControlEntry   *rce = &rcc->entry[s->picture_number];
901   |  
902   |     s->f_code = rce->f_code;
903   |     s->b_code = rce->b_code;
904   | }
905   |  
906   | // FIXME rd or at least approx for dquant
907   |  
908   | float ff_rate_estimate_qscale(MPVMainEncContext *const m, int dry_run)
909   | {
910   |  MPVEncContext  *const s = &m->s;
911   |     RateControlContext *rcc = &m->rc_context;
912   |     AVCodecContext *const a = s->c.avctx;
913   |  float q;
914   |  int qmin, qmax;
915   |  float br_compensation;
916   |  double diff;
917   |  double short_term_q;
918   |  double fps;
919   |  int picture_number = s->picture_number;
920   |     int64_t wanted_bits;
921   |     RateControlEntry local_rce, *rce;
922   |  double bits;
923   |  double rate_factor;
924   |     int64_t var;
925   |  const int pict_type = s->c.pict_type;
926   |  
927   |     get_qminmax(&qmin, &qmax, m, pict_type);
928   |  
929   |     fps = get_fps(s->c.avctx);
930   |  /* update predictors */
931   |  if (picture_number > 2 && !dry_run) {
    1Assuming 'picture_number' is <= 2→
932   |  const int64_t last_var =
933   |             m->last_pict_type == AV_PICTURE_TYPE_I ? rcc->last_mb_var_sum
934   |                                                    : rcc->last_mc_mb_var_sum;
935   |  av_assert1(m->frame_bits >= m->stuffing_bits);
936   |         update_predictor(&rcc->pred[m->last_pict_type],
937   |                          rcc->last_qscale,
938   |                          sqrt(last_var),
939   |                          m->frame_bits - m->stuffing_bits);
940   |     }
941   |  
942   |  if (s->c.avctx->flags & AV_CODEC_FLAG_PASS2) {
    2←Assuming the condition is true→
    3←Taking true branch→
943   |  av_assert0(picture_number >= 0);
    4←Assuming 'picture_number' is >= 0→
    5←Taking false branch→
    6←Loop condition is false.  Exiting loop→
944   |  if (picture_number >= rcc->num_entries) {
    7←Assuming 'picture_number' is < field 'num_entries'→
    8←Taking false branch→
945   |             av_log(s->c.avctx, AV_LOG_ERROR, "Input is longer than 2-pass log file\n");
946   |  return -1;
947   |         }
948   |  rce         = &rcc->entry[picture_number];
949   |  wanted_bits = rce->expected_bits;
950   |     } else {
951   |  const MPVPicture *dts_pic;
952   |  double wanted_bits_double;
953   |         rce = &local_rce;
954   |  
955   |  /* FIXME add a dts field to AVFrame and ensure it is set and use it
956   |  * here instead of reordering but the reordering is simpler for now
957   |  * until H.264 B-pyramid must be handled. */
958   |  if (s->c.pict_type == AV_PICTURE_TYPE_B || s->c.low_delay)
959   |             dts_pic = s->c.cur_pic.ptr;
960   |  else
961   |             dts_pic = s->c.last_pic.ptr;
962   |  
963   |  if (!dts_pic || dts_pic->f->pts == AV_NOPTS_VALUE)
964   |             wanted_bits_double = m->bit_rate * (double)picture_number / fps;
965   |  else
966   |             wanted_bits_double = m->bit_rate * (double)dts_pic->f->pts / fps;
967   |  if (wanted_bits_double > INT64_MAX) {
968   |             av_log(s->c.avctx, AV_LOG_WARNING, "Bits exceed 64bit range\n");
969   |             wanted_bits = INT64_MAX;
970   |         } else
971   |             wanted_bits = (int64_t)wanted_bits_double;
972   |     }
973   |  
974   |  diff = m->total_bits - wanted_bits;
975   |     br_compensation = (a->bit_rate_tolerance - diff) / a->bit_rate_tolerance;
976   |  if (br_compensation <= 0.0)
    9←Assuming the condition is false→
977   |         br_compensation = 0.001;
978   |  
979   |  var = pict_type10.1'pict_type' is not equal to AV_PICTURE_TYPE_I == AV_PICTURE_TYPE_I ? m->mb_var_sum : m->mc_mb_var_sum;
    10←Taking false branch→
    11←'?' condition is false→
980   |  
981   |  short_term_q = 0; /* avoid warning */
982   |  if (s->c.avctx->flags & AV_CODEC_FLAG_PASS2) {
    12←Taking true branch→
983   |  if (pict_type12.1'pict_type' is not equal to AV_PICTURE_TYPE_I != AV_PICTURE_TYPE_I)
    13←Taking true branch→
984   |  av_assert0(pict_type == rce->new_pict_type);
    14←Assuming 'pict_type' is equal to field 'new_pict_type'→
    15←Taking false branch→
    16←Loop condition is false.  Exiting loop→
985   |  
986   |  q = rce->new_qscale / br_compensation;
987   |  ff_dlog(s->c.avctx, "%f %f %f last:%d var:%"PRId64" type:%d//\n", q, rce->new_qscale,
    17←Taking false branch→
    18←Loop condition is false.  Exiting loop→
988   |  br_compensation, m->frame_bits, var, pict_type);
989   |     } else {
990   |         rce->pict_type     =
991   |         rce->new_pict_type = pict_type;
992   |         rce->mc_mb_var_sum = m->mc_mb_var_sum;
993   |         rce->mb_var_sum    = m->mb_var_sum;
994   |         rce->qscale        = FF_QP2LAMBDA * 2;
995   |         rce->f_code        = s->f_code;
996   |         rce->b_code        = s->b_code;
997   |         rce->misc_bits     = 1;
998   |  
999   |         bits = predict_size(&rcc->pred[pict_type], rce->qscale, sqrt(var));
1000  |  if (pict_type == AV_PICTURE_TYPE_I) {
1001  |             rce->i_count    = s->c.mb_num;
1002  |             rce->i_tex_bits = bits;
1003  |             rce->p_tex_bits = 0;
1004  |             rce->mv_bits    = 0;
1005  |         } else {
1006  |             rce->i_count    = 0;    // FIXME we do know this approx
1007  |             rce->i_tex_bits = 0;
1008  |             rce->p_tex_bits = bits * 0.9;
1009  |             rce->mv_bits    = bits * 0.1;
1010  |         }
1011  |         rcc->i_cplx_sum[pict_type]  += rce->i_tex_bits * rce->qscale;
1012  |         rcc->p_cplx_sum[pict_type]  += rce->p_tex_bits * rce->qscale;
1013  |         rcc->mv_bits_sum[pict_type] += rce->mv_bits;
1014  |         rcc->frame_count[pict_type]++;
1015  |  
1016  |         rate_factor = rcc->pass1_wanted_bits /
1017  |                       rcc->pass1_rc_eq_output_sum * br_compensation;
1018  |  
1019  |         q = get_qscale(m, rce, rate_factor, picture_number);
1020  |  if (q < 0)
1021  |  return -1;
1022  |  
1023  |  av_assert0(q > 0.0);
1024  |         q = get_diff_limited_q(m, rce, q);
1025  |  av_assert0(q > 0.0);
1026  |  
1027  |  // FIXME type dependent blur like in 2-pass
1028  |  if (pict_type == AV_PICTURE_TYPE_P || m->intra_only) {
1029  |             rcc->short_term_qsum   *= a->qblur;
1030  |             rcc->short_term_qcount *= a->qblur;
1031  |  
1032  |             rcc->short_term_qsum += q;
1033  |             rcc->short_term_qcount++;
1034  |             q = short_term_q = rcc->short_term_qsum / rcc->short_term_qcount;
1035  |         }
1036  |  av_assert0(q > 0.0);
1037  |  
1038  |         q = modify_qscale(m, rce, q, picture_number);
1039  |  
1040  |         rcc->pass1_wanted_bits += m->bit_rate / fps;
1041  |  
1042  |  av_assert0(q > 0.0);
1043  |     }
1044  |  
1045  |  if (s->c.avctx->debug & FF_DEBUG_RC) {
    19←Assuming the condition is false→
    20←Taking false branch→
1046  |         av_log(s->c.avctx, AV_LOG_DEBUG,
1047  |  "%c qp:%d<%2.1f<%d %d want:%"PRId64" total:%"PRId64" comp:%f st_q:%2.2f "
1048  |  "size:%d var:%"PRId64"/%"PRId64" br:%"PRId64" fps:%d\n",
1049  |                av_get_picture_type_char(pict_type),
1050  |                qmin, q, qmax, picture_number,
1051  |                wanted_bits / 1000, m->total_bits / 1000,
1052  |                br_compensation, short_term_q, m->frame_bits,
1053  |                m->mb_var_sum, m->mc_mb_var_sum,
1054  |                m->bit_rate / 1000, (int)fps);
1055  |     }
1056  |  
1057  |  if (q < qmin)
    21←Assuming 'q' is >= 'qmin'→
    22←Taking false branch→
1058  |         q = qmin;
1059  |  else if (q > qmax)
    23←Assuming 'q' is <= 'qmax'→
    24←Taking false branch→
1060  |         q = qmax;
1061  |  
1062  |  if (s->adaptive_quant)
    25←Assuming field 'adaptive_quant' is not equal to 0→
    26←Taking true branch→
1063  |  adaptive_quantization(rcc, m, q);
    27←Calling 'adaptive_quantization'→
1064  |  else
1065  |         q = (int)(q + 0.5);
1066  |  
1067  |  if (!dry_run) {
1068  |         rcc->last_qscale        = q;
1069  |         rcc->last_mc_mb_var_sum = m->mc_mb_var_sum;
1070  |         rcc->last_mb_var_sum    = m->mb_var_sum;
1071  |     }
1072  |  return q;
1073  | }