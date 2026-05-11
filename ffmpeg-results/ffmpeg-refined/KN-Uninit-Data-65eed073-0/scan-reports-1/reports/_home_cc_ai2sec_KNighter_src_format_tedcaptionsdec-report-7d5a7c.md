### Report Summary

File:| format/tedcaptionsdec.c  
---|---  
Warning:| line 75, column 27  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


21    |  
22    | #include "libavutil/bprint.h"
23    | #include "libavutil/log.h"
24    | #include "libavutil/opt.h"
25    | #include "avformat.h"
26    | #include "demux.h"
27    | #include "internal.h"
28    | #include "subtitles.h"
29    |  
30    | typedef struct {
31    |     AVClass *class;
32    |     int64_t start_time;
33    |     FFDemuxSubtitlesQueue subs;
34    | } TEDCaptionsDemuxer;
35    |  
36    | static const AVOption tedcaptions_options[] = {
37    |     { "start_time", "set the start time (offset) of the subtitles, in ms",
38    |  offsetof(TEDCaptionsDemuxer, start_time), AV_OPT_TYPE_INT64,
39    |       { .i64 = 15000 }, INT64_MIN, INT64_MAX,
40    |  AV_OPT_FLAG_SUBTITLE_PARAM | AV_OPT_FLAG_DECODING_PARAM },
41    |     { NULL },
42    | };
43    |  
44    | static const AVClass tedcaptions_demuxer_class = {
45    |     .class_name = "tedcaptions_demuxer",
46    |     .item_name  = av_default_item_name,
47    |     .option     = tedcaptions_options,
48    |     .version    = LIBAVUTIL_VERSION_INT,
49    | };
50    |  
51    | #define BETWEEN(a, amin, amax) ((unsigned)((a) - (amin)) <= (amax) - (amin))
52    |  
53    | #define HEX_DIGIT_TEST(c) (BETWEEN(c, '0', '9') || BETWEEN((c) | 32, 'a', 'z'))
54    | #define HEX_DIGIT_VAL(c) ((c) <= '9' ? (c) - '0' : ((c) | 32) - 'a' + 10)
55    | #define ERR_CODE(c) ((c) < 0 ? (c) : AVERROR_INVALIDDATA)
56    |  
57    | static void av_bprint_utf8(AVBPrint *bp, unsigned c)
58    | {
59    |  int bytes, i;
60    |  
61    |  if (c <= 0x7F) {
62    |         av_bprint_chars(bp, c, 1);
63    |  return;
64    |     }
65    |     bytes = (av_log2(c) - 2) / 5;
66    |     av_bprint_chars(bp, (c >> (bytes * 6)) | ((0xFF80 >> bytes) & 0xFF), 1);
67    |  for (i = bytes - 1; i >= 0; i--)
68    |         av_bprint_chars(bp, ((c >> (i * 6)) & 0x3F) | 0x80, 1);
69    | }
70    |  
71    | static void next_byte(AVIOContext *pb, int *cur_byte)
72    | {
73    |  uint8_t b;
74    |  int ret = avio_read(pb, &b, 1);
75    |  *cur_byte = ret > 0 ? b : ret == 0 ? AVERROR_EOF : ret;
    5←Assuming 'ret' is > 0→
    6←'?' condition is true→
    7←buffer read by avio_read may be partially uninitialized
76    | }
77    |  
78    | static void skip_spaces(AVIOContext *pb, int *cur_byte)
79    | {
80    |  while (*cur_byte == ' '  || *cur_byte == '\t' ||
81    |            *cur_byte == '\n' || *cur_byte == '\r')
82    |         next_byte(pb, cur_byte);
83    | }
84    |  
85    | static int expect_byte(AVIOContext *pb, int *cur_byte, uint8_t c)
86    | {
87    |     skip_spaces(pb, cur_byte);
88    |  if (*cur_byte != c)
89    |  return ERR_CODE(*cur_byte);
90    |     next_byte(pb, cur_byte);
91    |  return 0;
92    | }
93    |  
94    | static int parse_string(AVIOContext *pb, int *cur_byte, AVBPrint *bp, int full)
95    | {
96    |  int ret;
97    |  
98    |     ret = expect_byte(pb, cur_byte, '"');
99    |  if (ret < 0)
100   |  return ret;
101   |  while (*cur_byte > 0 && *cur_byte != '"') {
102   |  if (*cur_byte == '\\') {
103   |             next_byte(pb, cur_byte);
104   |  if (*cur_byte < 0)
105   |  return AVERROR_INVALIDDATA;
135   |  
136   |     av_bprint_init(bp, 0, AV_BPRINT_SIZE_AUTOMATIC);
137   |     ret = parse_string(pb, cur_byte, bp, 0);
138   |  if (ret < 0)
139   |  return ret;
140   |     ret = expect_byte(pb, cur_byte, ':');
141   |  if (ret < 0)
142   |  return ret;
143   |  return 0;
144   | }
145   |  
146   | static int parse_boolean(AVIOContext *pb, int *cur_byte, int *result)
147   | {
148   |  static const char * const text[] = { "false", "true" };
149   |  const char *p;
150   |  int i;
151   |  
152   |     skip_spaces(pb, cur_byte);
153   |  for (i = 0; i < 2; i++) {
154   |         p = text[i];
155   |  if (*cur_byte != *p)
156   |  continue;
157   |  for (; *p; p++, next_byte(pb, cur_byte))
158   |  if (*cur_byte != *p)
159   |  return AVERROR_INVALIDDATA;
160   |  if (BETWEEN(*cur_byte | 32, 'a', 'z'))
161   |  return AVERROR_INVALIDDATA;
162   |         *result = i;
163   |  return 0;
164   |     }
165   |  return AVERROR_INVALIDDATA;
166   | }
167   |  
168   | static int parse_int(AVIOContext *pb, int *cur_byte, int64_t *result)
169   | {
170   |     int64_t val = 0;
171   |  
172   |     skip_spaces(pb, cur_byte);
173   |  if ((unsigned)*cur_byte - '0' > 9)
174   |  return AVERROR_INVALIDDATA;
175   |  while (BETWEEN(*cur_byte, '0', '9')) {
176   |  if (val > INT_MAX/10 - (*cur_byte - '0'))
177   |  return AVERROR_INVALIDDATA;
178   |         val = val * 10 + (*cur_byte - '0');
179   |         next_byte(pb, cur_byte);
180   |     }
181   |     *result = val;
182   |  return 0;
183   | }
184   |  
185   | static int parse_file(AVIOContext *pb, FFDemuxSubtitlesQueue *subs)
186   | {
187   |  int ret, cur_byte, start_of_par;
188   |     AVBPrint label, content;
189   |     int64_t pos, start, duration;
190   |     AVPacket *pkt;
191   |  
192   |     av_bprint_init(&content, 0, AV_BPRINT_SIZE_UNLIMITED);
193   |  
194   |  next_byte(pb, &cur_byte);
    4←Calling 'next_byte'→
195   |     ret = expect_byte(pb, &cur_byte, '{');
196   |  if (ret < 0)
197   |  return AVERROR_INVALIDDATA;
198   |     ret = parse_label(pb, &cur_byte, &label);
199   |  if (ret < 0 || strcmp(label.str, "captions"))
200   |  return AVERROR_INVALIDDATA;
201   |     ret = expect_byte(pb, &cur_byte, '[');
202   |  if (ret < 0)
203   |  return AVERROR_INVALIDDATA;
204   |  while (1) {
205   |         start = duration = AV_NOPTS_VALUE;
206   |         ret = expect_byte(pb, &cur_byte, '{');
207   |  if (ret < 0)
208   |  goto fail;
209   |         pos = avio_tell(pb) - 1;
210   |  while (1) {
211   |             ret = parse_label(pb, &cur_byte, &label);
212   |  if (ret < 0)
213   |  goto fail;
214   |  if (!strcmp(label.str, "startOfParagraph")) {
215   |                 ret = parse_boolean(pb, &cur_byte, &start_of_par);
216   |  if (ret < 0)
217   |  goto fail;
218   |             } else if (!strcmp(label.str, "content")) {
219   |                 ret = parse_string(pb, &cur_byte, &content, 1);
220   |  if (ret < 0)
221   |  goto fail;
222   |             } else if (!strcmp(label.str, "startTime")) {
223   |                 ret = parse_int(pb, &cur_byte, &start);
224   |  if (ret < 0)
227   |                 ret = parse_int(pb, &cur_byte, &duration);
228   |  if (ret < 0)
229   |  goto fail;
230   |             } else {
231   |                 ret = AVERROR_INVALIDDATA;
232   |  goto fail;
233   |             }
234   |             skip_spaces(pb, &cur_byte);
235   |  if (cur_byte != ',')
236   |  break;
237   |             next_byte(pb, &cur_byte);
238   |         }
239   |         ret = expect_byte(pb, &cur_byte, '}');
240   |  if (ret < 0)
241   |  goto fail;
242   |  
243   |  if (!content.size || start == AV_NOPTS_VALUE ||
244   |             duration == AV_NOPTS_VALUE) {
245   |             ret = AVERROR_INVALIDDATA;
246   |  goto fail;
247   |         }
248   |         pkt = ff_subtitles_queue_insert_bprint(subs, &content, 0);
249   |  if (!pkt) {
250   |             ret = AVERROR(ENOMEM);
251   |  goto fail;
252   |         }
253   |         pkt->pos      = pos;
254   |         pkt->pts      = start;
255   |         pkt->duration = duration;
256   |         av_bprint_clear(&content);
257   |  
258   |         skip_spaces(pb, &cur_byte);
259   |  if (cur_byte != ',')
260   |  break;
261   |         next_byte(pb, &cur_byte);
262   |     }
263   |     ret = expect_byte(pb, &cur_byte, ']');
264   |  if (ret < 0)
265   |  goto fail;
266   |     ret = expect_byte(pb, &cur_byte, '}');
267   |  if (ret < 0)
268   |  goto fail;
269   |     skip_spaces(pb, &cur_byte);
270   |  if (cur_byte != AVERROR_EOF)
271   |         ret = ERR_CODE(cur_byte);
272   | fail:
273   |     av_bprint_finalize(&content, NULL);
274   |  return ret;
275   | }
276   |  
277   | static av_cold int tedcaptions_read_header(AVFormatContext *avf)
278   | {
279   |  TEDCaptionsDemuxer *tc = avf->priv_data;
280   |     AVStream *st = avformat_new_stream(avf, NULL);
281   |     FFStream *sti;
282   |  int ret, i;
283   |     AVPacket *last;
284   |  
285   |  if (!st)
    1Assuming 'st' is non-null→
    2←Taking false branch→
286   |  return AVERROR(ENOMEM);
287   |  
288   |  sti = ffstream(st);
289   |  ret = parse_file(avf->pb, &tc->subs);
    3←Calling 'parse_file'→
290   |  if (ret < 0) {
291   |  if (ret == AVERROR_INVALIDDATA)
292   |             av_log(avf, AV_LOG_ERROR, "Syntax error near offset %"PRId64".\n",
293   |                    avio_tell(avf->pb));
294   |  return ret;
295   |     }
296   |     ff_subtitles_queue_finalize(avf, &tc->subs);
297   |  for (i = 0; i < tc->subs.nb_subs; i++)
298   |         tc->subs.subs[i]->pts += tc->start_time;
299   |  
300   |     last = tc->subs.subs[tc->subs.nb_subs - 1];
301   |     st->codecpar->codec_type     = AVMEDIA_TYPE_SUBTITLE;
302   |     st->codecpar->codec_id       = AV_CODEC_ID_TEXT;
303   |     avpriv_set_pts_info(st, 64, 1, 1000);
304   |     sti->probe_packets = 0;
305   |     st->start_time    = 0;
306   |     st->duration      = last->pts + last->duration;
307   |     sti->cur_dts      = 0;
308   |  
309   |  return 0;
310   | }
311   |  
312   | static int tedcaptions_read_packet(AVFormatContext *avf, AVPacket *packet)
313   | {
314   |     TEDCaptionsDemuxer *tc = avf->priv_data;
315   |  
316   |  return ff_subtitles_queue_read_packet(&tc->subs, packet);
317   | }
318   |  
319   | static int tedcaptions_read_close(AVFormatContext *avf)