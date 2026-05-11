### Report Summary

File:| avfilter/avf_avectorscope.c  
---|---  
Warning:| line 358, column 23  
division by possibly zero aggregate factor  
  
### Annotated Source Code


238   |  static const enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_NONE };
239   |  static const enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_RGBA, AV_PIX_FMT_NONE };
240   |  static const AVChannelLayout layouts[] = {
241   |  AV_CHANNEL_LAYOUT_STEREO,
242   |         { .nb_channels = 0 },
243   |     };
244   |  int ret;
245   |  
246   |     formats = ff_make_sample_format_list(sample_fmts);
247   |  if ((ret = ff_formats_ref         (formats, &cfg_in[0]->formats        )) < 0)
248   |  return ret;
249   |  
250   |     ret = ff_set_common_channel_layouts_from_list2(ctx, cfg_in, cfg_out, layouts);
251   |  if (ret < 0)
252   |  return ret;
253   |  
254   |     formats = ff_make_pixel_format_list(pix_fmts);
255   |  if ((ret = ff_formats_ref(formats, &cfg_out[0]->formats)) < 0)
256   |  return ret;
257   |  
258   |  return 0;
259   | }
260   |  
261   | static int config_input(AVFilterLink *inlink)
262   | {
263   |     AVFilterContext *ctx = inlink->dst;
264   |     AudioVectorScopeContext *s = ctx->priv;
265   |  
266   |     s->nb_samples = FFMAX(1, av_rescale(inlink->sample_rate, s->frame_rate.den, s->frame_rate.num));
267   |  
268   |  return 0;
269   | }
270   |  
271   | static int config_output(AVFilterLink *outlink)
272   | {
273   |     AudioVectorScopeContext *s = outlink->src->priv;
274   |     FilterLink *l = ff_filter_link(outlink);
275   |  
276   |     outlink->w = s->w;
277   |     outlink->h = s->h;
278   |     outlink->sample_aspect_ratio = (AVRational){1,1};
279   |     l->frame_rate = s->frame_rate;
280   |     outlink->time_base = av_inv_q(l->frame_rate);
281   |  
282   |     s->prev_x = s->hw = s->w / 2;
283   |     s->prev_y = s->hh = s->mode == POLAR ? s->h - 1 : s->h / 2;
284   |  
285   |  return 0;
286   | }
287   |  
288   | static int filter_frame(AVFilterLink *inlink, AVFrame *insamples)
289   | {
290   |  AVFilterContext *ctx = inlink->dst;
291   |     AVFilterLink *outlink = ctx->outputs[0];
292   |  const int16_t *samples = (const int16_t *)insamples->data[0];
293   |  const float *samplesf = (const float *)insamples->data[0];
294   |     AudioVectorScopeContext *s = ctx->priv;
295   |  const int hw = s->hw;
296   |  const int hh = s->hh;
297   |     AVFrame *clone;
298   |  unsigned x, y;
299   |  unsigned prev_x = s->prev_x, prev_y = s->prev_y;
300   |  double zoom = s->zoom;
301   |  int ret;
302   |  
303   |  if (!s->outpicref || s->outpicref->width  != outlink->w ||
    1Assuming field 'outpicref' is non-null→
    2←Assuming field 'width' is equal to field 'w'→
    4←Taking false branch→
304   |  s->outpicref->height != outlink->h) {
    3←Assuming field 'height' is equal to field 'h'→
305   |         av_frame_free(&s->outpicref);
306   |         s->outpicref = ff_get_video_buffer(outlink, outlink->w, outlink->h);
307   |  if (!s->outpicref) {
308   |             av_frame_free(&insamples);
309   |  return AVERROR(ENOMEM);
310   |         }
311   |  
312   |         s->outpicref->sample_aspect_ratio = (AVRational){1,1};
313   |  for (int i = 0; i < outlink->h; i++)
314   |             memset(s->outpicref->data[0] + i * s->outpicref->linesize[0], 0, outlink->w * 4);
315   |     }
316   |  s->outpicref->pts = av_rescale_q(insamples->pts, inlink->time_base, outlink->time_base);
317   |     s->outpicref->duration = 1;
318   |  
319   |     ret = ff_inlink_make_frame_writable(outlink, &s->outpicref);
320   |  if (ret < 0) {
    5←Assuming 'ret' is >= 0→
    6←Taking false branch→
321   |         av_frame_free(&insamples);
322   |  return ret;
323   |     }
324   |  ff_filter_execute(ctx, fade, NULL, NULL, FFMIN(outlink->h, ff_filter_get_nb_threads(ctx)));
    7←Assuming the condition is false→
    8←'?' condition is false→
325   |  
326   |  if (zoom < 1) {
    9←Assuming 'zoom' is < 1→
    10←Taking true branch→
327   |  float max = 0;
328   |  
329   |  switch (insamples->format) {
    11←Control jumps to the 'default' case at line 341→
330   |  case AV_SAMPLE_FMT_S16:
331   |  for (int i = 0; i < insamples->nb_samples * 2; i++) {
332   |  float sample = samples[i] / (float)INT16_MAX;
333   |                 max = FFMAX(FFABS(sample), max);
334   |             }
335   |  break;
336   |  case AV_SAMPLE_FMT_FLT:
337   |  for (int i = 0; i < insamples->nb_samples * 2; i++) {
338   |                 max = FFMAX(FFABS(samplesf[i]), max);
339   |             }
340   |  break;
341   |  default:
342   |  av_assert2(0);
343   |         }
344   |  
345   |  switch (s->scale) {
346   |  case SQRT:
347   |             max = sqrtf(max);
348   |  break;
349   |  case CBRT:
350   |             max = cbrtf(max);
351   |  break;
352   |  case LOG:
353   |             max = logf(1 + max) / logf(2);
354   |  break;
355   |         }
356   |  
357   |  if (max > 0.f)
    12←'Default' branch taken. Execution continues on line 357→
    13←Assuming the condition is true→
    14←Taking true branch→
358   |  zoom = 1. / max;
    15←division by possibly zero aggregate factor
359   |     }
360   |  
361   |  for (int i = 0; i < insamples->nb_samples; i++) {
362   |  float src[2];
363   |  
364   |  switch (insamples->format) {
365   |  case AV_SAMPLE_FMT_S16:
366   |             src[0] = samples[i*2+0] / (float)INT16_MAX;
367   |             src[1] = samples[i*2+1] / (float)INT16_MAX;
368   |  break;
369   |  case AV_SAMPLE_FMT_FLT:
370   |             src[0] = samplesf[i*2+0];
371   |             src[1] = samplesf[i*2+1];
372   |  break;
373   |  default:
374   |  av_assert2(0);
375   |         }
376   |  
377   |  switch (s->scale) {
378   |  case SQRT:
379   |             src[0] = FFSIGN(src[0]) * sqrtf(FFABS(src[0]));
380   |             src[1] = FFSIGN(src[1]) * sqrtf(FFABS(src[1]));
381   |  break;
382   |  case CBRT:
383   |             src[0] = FFSIGN(src[0]) * cbrtf(FFABS(src[0]));
384   |             src[1] = FFSIGN(src[1]) * cbrtf(FFABS(src[1]));
385   |  break;
386   |  case LOG:
387   |             src[0] = FFSIGN(src[0]) * logf(1 + FFABS(src[0])) / logf(2);
388   |             src[1] = FFSIGN(src[1]) * logf(1 + FFABS(src[1])) / logf(2);