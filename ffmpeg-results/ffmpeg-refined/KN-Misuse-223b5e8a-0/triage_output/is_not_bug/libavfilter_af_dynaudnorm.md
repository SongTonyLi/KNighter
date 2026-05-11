### Report Summary

File:| avfilter/af_dynaudnorm.c  
---|---  
Warning:| line 299, column 18  
division by possibly zero aggregate factor  
  
### Annotated Source Code


229   |  
230   | static double cqueue_peek(cqueue *q, int index)
231   | {
232   |  av_assert2(index < q->nb_elements);
233   |  return q->elements[index];
234   | }
235   |  
236   | static int cqueue_dequeue(cqueue *q, double *element)
237   | {
238   |  av_assert2(!cqueue_empty(q));
239   |  
240   |     *element = q->elements[0];
241   |     memmove(&q->elements[0], &q->elements[1], (q->nb_elements - 1) * sizeof(double));
242   |     q->nb_elements--;
243   |  
244   |  return 0;
245   | }
246   |  
247   | static int cqueue_pop(cqueue *q)
248   | {
249   |  av_assert2(!cqueue_empty(q));
250   |  
251   |     memmove(&q->elements[0], &q->elements[1], (q->nb_elements - 1) * sizeof(double));
252   |     q->nb_elements--;
253   |  
254   |  return 0;
255   | }
256   |  
257   | static void cqueue_resize(cqueue *q, int new_size)
258   | {
259   |  av_assert2(q->max_size >= new_size);
260   |  av_assert2(MIN_FILTER_SIZE <= new_size);
261   |  
262   |  if (new_size > q->nb_elements) {
263   |  const int side = (new_size - q->nb_elements) / 2;
264   |  
265   |         memmove(q->elements + side, q->elements, sizeof(double) * q->nb_elements);
266   |  for (int i = 0; i < side; i++)
267   |             q->elements[i] = q->elements[side];
268   |         q->nb_elements = new_size - 1 - side;
269   |     } else {
270   |  int count = (q->size - new_size + 1) / 2;
271   |  
272   |  while (count-- > 0)
273   |             cqueue_pop(q);
274   |     }
275   |  
276   |     q->size = new_size;
277   | }
278   |  
279   | static void init_gaussian_filter(DynamicAudioNormalizerContext *s)
280   | {
281   |  double total_weight = 0.0;
282   |  const double sigma = (((s->filter_size / 2.0) - 1.0) / 3.0) + (1.0 / 3.0);
283   |  double adjust;
284   |  
285   |  // Pre-compute constants
286   |  const int offset = s->filter_size / 2;
287   |  const double c1 = 1.0 / (sigma * sqrt(2.0 * M_PI));
288   |  const double c2 = 2.0 * sigma * sigma;
289   |  
290   |  // Compute weights
291   |  for (int i = 0; i < s->filter_size; i++) {
    6←Assuming 'i' is >= field 'filter_size'→
    7←Loop condition is false. Execution continues on line 299→
292   |  const int x = i - offset;
293   |  
294   |         s->weights[i] = c1 * exp(-x * x / c2);
295   |         total_weight += s->weights[i];
296   |     }
297   |  
298   |  // Adjust weights
299   |  adjust = 1.0 / total_weight;
    8←division by possibly zero aggregate factor
300   |  for (int i = 0; i < s->filter_size; i++) {
301   |         s->weights[i] *= adjust;
302   |     }
303   | }
304   |  
305   | static av_cold void uninit(AVFilterContext *ctx)
306   | {
307   |     DynamicAudioNormalizerContext *s = ctx->priv;
308   |  
309   |     av_freep(&s->prev_amplification_factor);
310   |     av_freep(&s->dc_correction_value);
311   |     av_freep(&s->compress_threshold);
312   |  
313   |  for (int c = 0; c < s->channels; c++) {
314   |  if (s->gain_history_original)
315   |             cqueue_free(s->gain_history_original[c]);
316   |  if (s->gain_history_minimum)
317   |             cqueue_free(s->gain_history_minimum[c]);
318   |  if (s->gain_history_smoothed)
319   |             cqueue_free(s->gain_history_smoothed[c]);
320   |  if (s->threshold_history)
321   |             cqueue_free(s->threshold_history[c]);
322   |     }
323   |  
324   |     av_freep(&s->gain_history_original);
325   |     av_freep(&s->gain_history_minimum);
326   |     av_freep(&s->gain_history_smoothed);
327   |     av_freep(&s->threshold_history);
328   |  
329   |     cqueue_free(s->is_enabled);
931   |     AVFilterLink *inlink = ctx->inputs[0];
932   |     AVFilterLink *outlink = ctx->outputs[0];
933   |     DynamicAudioNormalizerContext *s = ctx->priv;
934   |     AVFrame *in = NULL;
935   |  int ret = 0, status;
936   |     int64_t pts;
937   |  
938   |     ret = av_channel_layout_copy(&s->ch_layout, &inlink->ch_layout);
939   |  if (ret < 0)
940   |  return ret;
941   |  if (strcmp(s->channels_to_filter, "all"))
942   |         av_channel_layout_from_string(&s->ch_layout, s->channels_to_filter);
943   |  
944   |  FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);
945   |  
946   |  if (!s->eof) {
947   |         ret = ff_inlink_consume_samples(inlink, s->sample_advance, s->sample_advance, &in);
948   |  if (ret < 0)
949   |  return ret;
950   |  if (ret > 0) {
951   |             ret = filter_frame(inlink, in);
952   |  if (ret <= 0)
953   |  return ret;
954   |         }
955   |  
956   |  if (ff_inlink_check_available_samples(inlink, s->sample_advance) > 0) {
957   |             ff_filter_set_ready(ctx, 10);
958   |  return 0;
959   |         }
960   |     }
961   |  
962   |  if (!s->eof && ff_inlink_acknowledge_status(inlink, &status, &pts)) {
963   |  if (status == AVERROR_EOF)
964   |             s->eof = 1;
965   |     }
966   |  
967   |  if (s->eof && s->queue.available)
968   |  return flush(outlink);
969   |  
970   |  if (s->eof && !s->queue.available) {
971   |         ff_outlink_set_status(outlink, AVERROR_EOF, s->pts);
972   |  return 0;
973   |     }
974   |  
975   |  if (!s->eof)
976   |  FF_FILTER_FORWARD_WANTED(outlink, inlink);
977   |  
978   |  return FFERROR_NOT_READY;
979   | }
980   |  
981   | static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
982   |  char *res, int res_len, int flags)
983   | {
984   |  DynamicAudioNormalizerContext *s = ctx->priv;
985   |     AVFilterLink *inlink = ctx->inputs[0];
986   |  int prev_filter_size = s->filter_size;
987   |  int ret;
988   |  
989   |     ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);
990   |  if (ret < 0)
    1Assuming 'ret' is >= 0→
    2←Taking false branch→
991   |  return ret;
992   |  
993   |  s->filter_size |= 1;
994   |  if (prev_filter_size != s->filter_size) {
    3←Assuming 'prev_filter_size' is not equal to field 'filter_size'→
    4←Taking true branch→
995   |  init_gaussian_filter(s);
    5←Calling 'init_gaussian_filter'→
996   |  
997   |  for (int c = 0; c < s->channels; c++) {
998   |             cqueue_resize(s->gain_history_original[c], s->filter_size);
999   |             cqueue_resize(s->gain_history_minimum[c], s->filter_size);
1000  |             cqueue_resize(s->threshold_history[c], s->filter_size);
1001  |         }
1002  |     }
1003  |  
1004  |     s->frame_len = frame_size(inlink->sample_rate, s->frame_len_msec);
1005  |     s->sample_advance = FFMAX(1, lrint(s->frame_len * (1. - s->overlap)));
1006  |  if (s->expr_str) {
1007  |         ret = av_expr_parse(&s->expr, s->expr_str, var_names, NULL, NULL,
1008  |  NULL, NULL, 0, ctx);
1009  |  if (ret < 0)
1010  |  return ret;
1011  |     }
1012  |  return 0;
1013  | }
1014  |  
1015  | static const AVFilterPad avfilter_af_dynaudnorm_inputs[] = {
1016  |     {
1017  |         .name           = "default",
1018  |         .type           = AVMEDIA_TYPE_AUDIO,
1019  |         .config_props   = config_input,
1020  |     },
1021  | };
1022  |  
1023  | const FFFilter ff_af_dynaudnorm = {
1024  |     .p.name        = "dynaudnorm",
1025  |     .p.description = NULL_IF_CONFIG_SMALL("Dynamic Audio Normalizer."),