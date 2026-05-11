### Report Summary

File:| avfilter/af_anlmdn.c  
---|---  
Warning:| line 260, column 49  
division by possibly zero aggregate factor  
  
### Annotated Source Code


148   |         }
149   |     }
150   |  if (!s->cache)
151   |  return AVERROR(ENOMEM);
152   |  
153   |  if (!s->window || s->window->nb_samples < newN) {
154   |         AVFrame *new_window = ff_get_audio_buffer(outlink, newN);
155   |  if (new_window) {
156   |  if (s->window)
157   |                 av_samples_copy(new_window->extended_data, s->window->extended_data, 0, 0,
158   |                                 s->window->nb_samples, new_window->ch_layout.nb_channels, new_window->format);
159   |             av_frame_free(&s->window);
160   |             s->window = new_window;
161   |         } else {
162   |  return AVERROR(ENOMEM);
163   |         }
164   |     }
165   |  if (!s->window)
166   |  return AVERROR(ENOMEM);
167   |  
168   |     s->pdiff_lut_scale = 1.f / s->m * WEIGHT_LUT_SIZE;
169   |  for (int i = 0; i < WEIGHT_LUT_SIZE; i++) {
170   |  float w = -i / s->pdiff_lut_scale;
171   |  
172   |         s->weight_lut[i] = expf(w);
173   |     }
174   |  
175   |     s->K = newK;
176   |     s->S = newS;
177   |     s->H = newH;
178   |     s->N = newN;
179   |  
180   |  return 0;
181   | }
182   |  
183   | static int config_output(AVFilterLink *outlink)
184   | {
185   |     AVFilterContext *ctx = outlink->src;
186   |     AudioNLMeansContext *s = ctx->priv;
187   |  int ret;
188   |  
189   |     ret = config_filter(ctx);
190   |  if (ret < 0)
191   |  return ret;
192   |  
193   |     ff_anlmdn_init(&s->dsp);
194   |  
195   |  return 0;
196   | }
197   |  
198   | static int filter_channel(AVFilterContext *ctx, void *arg, int ch, int nb_jobs)
199   | {
200   |  AudioNLMeansContext *s = ctx->priv;
201   |     AVFrame *out = arg;
202   |  const int S = s->S;
203   |  const int K = s->K;
204   |  const int N = s->N;
205   |  const int H = s->H;
206   |  const int om = s->om;
207   |  const float *f = (const float *)(s->window->extended_data[ch]) + K;
208   |  float *cache = (float *)s->cache->extended_data[ch];
209   |  const float sw = (65536.f / (4 * K + 2)) / sqrtf(s->a);
210   |  float *dst = (float *)out->extended_data[ch];
211   |  const float *const weight_lut = s->weight_lut;
212   |  const float pdiff_lut_scale = s->pdiff_lut_scale;
213   |  const float smooth = fminf(s->m, WEIGHT_LUT_SIZE / pdiff_lut_scale);
214   |  const int offset = N - H;
215   |  float *src = (float *)s->window->extended_data[ch];
216   |  const AVFrame *const in = s->in;
217   |  
218   |     memmove(src, &src[H], offset * sizeof(float));
219   |     memcpy(&src[offset], in->extended_data[ch], in->nb_samples * sizeof(float));
220   |     memset(&src[offset + in->nb_samples], 0, (H - in->nb_samples) * sizeof(float));
221   |  
222   |  for (int i = S; i < H + S; i++) {
    1Assuming the condition is true→
    2←Loop condition is true.  Entering loop body→
223   |  float P = 0.f, Q = 0.f;
224   |  int v = 0;
225   |  
226   |  if (i2.1'i' is equal to 'S' == S) {
    3←Taking true branch→
227   |  for (int j = i - S; j <= i + S; j++) {
    4←Assuming the condition is false→
    5←Loop condition is false. Execution continues on line 237→
228   |  if (i == j)
229   |  continue;
230   |                 cache[v++] = s->dsp.compute_distance_ssd(f + i, f + j, K);
231   |             }
232   |         } else {
233   |             s->dsp.compute_cache(cache, f, S, K, i, i - S);
234   |             s->dsp.compute_cache(cache + S, f, S, K, i, i + 1);
235   |         }
236   |  
237   |  for (int j = 0; j < 2 * S && !ctx->is_disabled; j++) {
    6←Assuming the condition is false→
238   |  float distance = cache[j];
239   |  unsigned weight_lut_idx;
240   |  float w;
241   |  
242   |  if (distance < 0.f)
243   |                 cache[j] = distance = 0.f;
244   |             w = distance * sw;
245   |  if (w >= smooth)
246   |  continue;
247   |             weight_lut_idx = w * pdiff_lut_scale;
248   |  av_assert2(weight_lut_idx < WEIGHT_LUT_SIZE);
249   |             w = weight_lut[weight_lut_idx];
250   |             P += w * f[i - S + j + (j >= S)];
251   |             Q += w;
252   |         }
253   |  
254   |  P += f[i];
255   |         Q += 1.f;
256   |  
257   |  switch (om) {
    7←Control jumps to 'case NOISE_MODE:'  at line 260→
258   |  case IN_MODE:    dst[i - S] = f[i];           break;
259   |  case OUT_MODE:   dst[i - S] = P / Q;          break;
260   |  case NOISE_MODE: dst[i - S] = f[i] - (P / Q); break;
    8←division by possibly zero aggregate factor
261   |         }
262   |     }
263   |  
264   |  return 0;
265   | }
266   |  
267   | static int filter_frame(AVFilterLink *inlink, AVFrame *in)
268   | {
269   |     AVFilterContext *ctx = inlink->dst;
270   |     AVFilterLink *outlink = ctx->outputs[0];
271   |     AudioNLMeansContext *s = ctx->priv;
272   |     AVFrame *out;
273   |  
274   |  if (av_frame_is_writable(in)) {
275   |         out = in;
276   |     } else {
277   |         out = ff_get_audio_buffer(outlink, in->nb_samples);
278   |  if (!out) {
279   |             av_frame_free(&in);
280   |  return AVERROR(ENOMEM);
281   |         }
282   |  
283   |         out->pts = in->pts;
284   |     }
285   |  
286   |     s->in = in;
287   |     ff_filter_execute(ctx, filter_channel, out, NULL, inlink->ch_layout.nb_channels);
288   |  
289   |  if (out != in)
290   |         av_frame_free(&in);