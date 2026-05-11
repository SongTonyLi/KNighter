### Report Summary

File:| avfilter/af_alimiter.c  
---|---  
Warning:| line 261, column 32  
division by possibly zero aggregate factor  
  
### Annotated Source Code


78    | #define OFFSET(x) offsetof(AudioLimiterContext, x)
79    | #define AF AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_RUNTIME_PARAM
80    |  
81    | static const AVOption alimiter_options[] = {
82    |     { "level_in",  "set input level",  OFFSET(level_in),     AV_OPT_TYPE_DOUBLE, {.dbl=1},.015625,   64, AF },
83    |     { "level_out", "set output level", OFFSET(level_out),    AV_OPT_TYPE_DOUBLE, {.dbl=1},.015625,   64, AF },
84    |     { "limit",     "set limit",        OFFSET(limit),        AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0.0625,    1, AF },
85    |     { "attack",    "set attack",       OFFSET(attack),       AV_OPT_TYPE_DOUBLE, {.dbl=5},    0.1,   80, AF },
86    |     { "release",   "set release",      OFFSET(release),      AV_OPT_TYPE_DOUBLE, {.dbl=50},     1, 8000, AF },
87    |     { "asc",       "enable asc",       OFFSET(auto_release), AV_OPT_TYPE_BOOL,   {.i64=0},      0,    1, AF },
88    |     { "asc_level", "set asc level",    OFFSET(asc_coeff),    AV_OPT_TYPE_DOUBLE, {.dbl=0.5},    0,    1, AF },
89    |     { "level",     "auto level",       OFFSET(auto_level),   AV_OPT_TYPE_BOOL,   {.i64=1},      0,    1, AF },
90    |     { "latency",   "compensate delay", OFFSET(latency),      AV_OPT_TYPE_BOOL,   {.i64=0},      0,    1, AF },
91    |     { NULL }
92    | };
93    |  
94    | AVFILTER_DEFINE_CLASS(alimiter);
95    |  
96    | static av_cold int init(AVFilterContext *ctx)
97    | {
98    |     AudioLimiterContext *s = ctx->priv;
99    |  
100   |     s->attack   /= 1000.;
101   |     s->release  /= 1000.;
102   |     s->att       = 1.;
103   |     s->asc_pos   = -1;
104   |     s->asc_coeff = pow(0.5, s->asc_coeff - 0.5) * 2 * -1;
105   |  
106   |  return 0;
107   | }
108   |  
109   | static double get_rdelta(AudioLimiterContext *s, double release, int sample_rate,
110   |  double peak, double limit, double patt, int asc)
111   | {
112   |  double rdelta = (1.0 - patt) / (sample_rate * release);
113   |  
114   |  if (asc && s->auto_release && s->asc_c > 0) {
115   |  double a_att = limit / (s->asc_coeff * s->asc) * (double)s->asc_c;
116   |  
117   |  if (a_att > patt) {
118   |  double delta = FFMAX((a_att - patt) / (sample_rate * release), rdelta / 10);
119   |  
120   |  if (delta < rdelta)
121   |                 rdelta = delta;
122   |         }
123   |     }
124   |  
125   |  return rdelta;
126   | }
127   |  
128   | static int filter_frame(AVFilterLink *inlink, AVFrame *in)
129   | {
130   |  AVFilterContext *ctx = inlink->dst;
131   |     AudioLimiterContext *s = ctx->priv;
132   |     AVFilterLink *outlink = ctx->outputs[0];
133   |  const double *src = (const double *)in->data[0];
134   |  const int channels = inlink->ch_layout.nb_channels;
135   |  const int buffer_size = s->buffer_size;
136   |  double *dst, *buffer = s->buffer;
137   |  const double release = s->release;
138   |  const double limit = s->limit;
139   |  double *nextdelta = s->nextdelta;
140   |  double level = s->auto_level ? 1 / limit : 1;
    1Assuming field 'auto_level' is 0→
    2←'?' condition is false→
141   |  const double level_out = s->level_out;
142   |  const double level_in = s->level_in;
143   |  int *nextpos = s->nextpos;
144   |     AVFrame *out;
145   |  double *buf;
146   |  int n, c, i;
147   |  int new_out_samples;
148   |     int64_t out_duration;
149   |     int64_t in_duration;
150   |     int64_t in_pts;
151   |     MetaItem meta;
152   |  
153   |  if (av_frame_is_writable(in)) {
    3←Assuming the condition is true→
    4←Taking true branch→
154   |  out = in;
155   |     } else {
156   |         out = ff_get_audio_buffer(outlink, in->nb_samples);
157   |  if (!out) {
158   |             av_frame_free(&in);
159   |  return AVERROR(ENOMEM);
160   |         }
161   |         av_frame_copy_props(out, in);
162   |     }
163   |  dst = (double *)out->data[0];
164   |  
165   |  for (n = 0; n < in->nb_samples; n++) {
    5←Assuming 'n' is < field 'nb_samples'→
    6←Loop condition is true.  Entering loop body→
166   |  double peak = 0;
167   |  
168   |  for (c = 0; c < channels; c++) {
    7←Assuming 'c' is >= 'channels'→
169   |  double sample = src[c] * level_in;
170   |  
171   |             buffer[s->pos + c] = sample;
172   |             peak = FFMAX(peak, fabs(sample));
173   |         }
174   |  
175   |  if (s->auto_release && peak > limit) {
    8←Assuming field 'auto_release' is 0→
176   |             s->asc += peak;
177   |             s->asc_c++;
178   |         }
179   |  
180   |  if (peak > limit) {
    9←Assuming 'peak' is <= 'limit'→
    10←Taking false branch→
181   |  double patt = FFMIN(limit / peak, 1.);
182   |  double rdelta = get_rdelta(s, release, inlink->sample_rate,
183   |                                        peak, limit, patt, 0);
184   |  double delta = (limit / peak - s->att) / buffer_size * channels;
185   |  int found = 0;
186   |  
187   |  if (delta < s->delta) {
188   |                 s->delta = delta;
189   |                 nextpos[0] = s->pos;
190   |                 nextpos[1] = -1;
191   |                 nextdelta[0] = rdelta;
192   |                 s->nextlen = 1;
193   |                 s->nextiter= 0;
194   |             } else {
195   |  for (i = s->nextiter; i < s->nextiter + s->nextlen; i++) {
196   |  int j = i % buffer_size;
197   |  double ppeak = 0, pdelta;
198   |  
199   |  if (nextpos[j] >= 0)
200   |  for (c = 0; c < channels; c++) {
201   |                             ppeak = FFMAX(ppeak, fabs(buffer[nextpos[j] + c]));
202   |                         }
203   |                     pdelta = (limit / peak - limit / ppeak) / (((buffer_size - nextpos[j] + s->pos) % buffer_size) / channels);
204   |  if (pdelta < nextdelta[j]) {
205   |                         nextdelta[j] = pdelta;
206   |                         found = 1;
207   |  break;
208   |                     }
209   |                 }
210   |  if (found) {
211   |                     s->nextlen = i - s->nextiter + 1;
212   |                     nextpos[(s->nextiter + s->nextlen) % buffer_size] = s->pos;
213   |                     nextdelta[(s->nextiter + s->nextlen) % buffer_size] = rdelta;
214   |                     nextpos[(s->nextiter + s->nextlen + 1) % buffer_size] = -1;
215   |                     s->nextlen++;
216   |                 }
217   |             }
218   |         }
219   |  
220   |  buf = &s->buffer[(s->pos + channels) % buffer_size];
221   |         peak = 0;
222   |  for (c = 0; c10.1'c' is >= 'channels' < channels; c++) {
223   |  double sample = buf[c];
224   |  
225   |             peak = FFMAX(peak, fabs(sample));
226   |         }
227   |  
228   |  if (s->pos == s->asc_pos && !s->asc_changed)
    11←Assuming field 'pos' is not equal to field 'asc_pos'→
229   |             s->asc_pos = -1;
230   |  
231   |  if (s->auto_release11.1Field 'auto_release' is 0 && s->asc_pos == -1 && peak > limit) {
232   |             s->asc -= peak;
233   |             s->asc_c--;
234   |         }
235   |  
236   |  s->att += s->delta;
237   |  
238   |  for (c = 0; c11.2'c' is >= 'channels' < channels; c++)
    12←Loop condition is false. Execution continues on line 241→
239   |             dst[c] = buf[c] * s->att;
240   |  
241   |  if ((s->pos + channels) % buffer_size == nextpos[s->nextiter]) {
    13←Assuming the condition is true→
    14←Taking true branch→
242   |  if (s->auto_release14.1Field 'auto_release' is 0) {
    15←Taking false branch→
243   |                 s->delta = get_rdelta(s, release, inlink->sample_rate,
244   |                                       peak, limit, s->att, 1);
245   |  if (s->nextlen > 1) {
246   |  double ppeak = 0, pdelta;
247   |  int pnextpos = nextpos[(s->nextiter + 1) % buffer_size];
248   |  
249   |  for (c = 0; c < channels; c++) {
250   |                         ppeak = FFMAX(ppeak, fabs(buffer[pnextpos + c]));
251   |                     }
252   |                     pdelta = (limit / ppeak - s->att) /
253   |                              (((buffer_size + pnextpos -
254   |                              ((s->pos + channels) % buffer_size)) %
255   |                              buffer_size) / channels);
256   |  if (pdelta < s->delta)
257   |                         s->delta = pdelta;
258   |                 }
259   |             } else {
260   |  s->delta = nextdelta[s->nextiter];
261   |                 s->att = limit / peak;
    16←division by possibly zero aggregate factor
262   |             }
263   |  
264   |             s->nextlen -= 1;
265   |             nextpos[s->nextiter] = -1;
266   |             s->nextiter = (s->nextiter + 1) % buffer_size;
267   |         }
268   |  
269   |  if (s->att > 1.) {
270   |             s->att = 1.;
271   |             s->delta = 0.;
272   |             s->nextiter = 0;
273   |             s->nextlen = 0;
274   |             nextpos[0] = -1;
275   |         }
276   |  
277   |  if (s->att <= 0.) {
278   |             s->att = 0.0000000000001;
279   |             s->delta = (1.0 - s->att) / (inlink->sample_rate * release);
280   |         }
281   |  
282   |  if (s->att != 1. && (1. - s->att) < 0.0000000000001)
283   |             s->att = 1.;
284   |  
285   |  if (s->delta != 0. && fabs(s->delta) < 0.00000000000001)
286   |             s->delta = 0.;
287   |  
288   |  for (c = 0; c < channels; c++)
289   |             dst[c] = av_clipd(dst[c], -limit, limit) * level * level_out;
290   |  
291   |         s->pos = (s->pos + channels) % buffer_size;