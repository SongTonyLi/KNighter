### Report Summary

File:| /home/cc/ai2sec/KNighter/ffmpeg/libavformat/mlvdec.c  
---|---  
Warning:| line 91, column 10  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


32    | #include "avio_internal.h"
33    | #include "internal.h"
34    | #include "riff.h"
35    |  
36    | #define MLV_VERSION "v2.0"
37    |  
38    | #define MLV_VIDEO_CLASS_RAW  1
39    | #define MLV_VIDEO_CLASS_YUV  2
40    | #define MLV_VIDEO_CLASS_JPEG 3
41    | #define MLV_VIDEO_CLASS_H264 4
42    |  
43    | #define MLV_AUDIO_CLASS_WAV  1
44    |  
45    | #define MLV_CLASS_FLAG_DELTA 0x40
46    | #define MLV_CLASS_FLAG_LZMA  0x80
47    |  
48    | typedef struct {
49    |     AVIOContext *pb[101];
50    |  int class[2];
51    |  int stream_index;
52    |     uint64_t pts;
53    | } MlvContext;
54    |  
55    | static int read_close(AVFormatContext *s);
56    |  
57    | static int probe(const AVProbeData *p)
58    | {
59    |  if (AV_RL32(p->buf) == MKTAG('M','L','V','I') &&
60    |  AV_RL32(p->buf + 4) >= 52 &&
61    |         !memcmp(p->buf + 8, MLV_VERSION, 5))
62    |  return AVPROBE_SCORE_MAX;
63    |  return 0;
64    | }
65    |  
66    | static int check_file_header(AVIOContext *pb, uint64_t guid)
67    | {
68    |  unsigned int size;
69    |     uint8_t version[8];
70    |  
71    |     avio_skip(pb, 4);
72    |     size = avio_rl32(pb);
73    |  if (size < 52)
74    |  return AVERROR_INVALIDDATA;
75    |     avio_read(pb, version, 8);
76    |  if (memcmp(version, MLV_VERSION, 5) || avio_rl64(pb) != guid)
77    |  return AVERROR_INVALIDDATA;
78    |     avio_skip(pb, size - 24);
79    |  return 0;
80    | }
81    |  
82    | static void read_string(AVFormatContext *avctx, AVIOContext *pb, const char *tag, unsigned size)
83    | {
84    |  char * value = av_malloc(size + 1);
85    |  if (!value) {
    16←Assuming 'value' is non-null→
    17←Taking false branch→
86    |         avio_skip(pb, size);
87    |  return;
88    |     }
89    |  
90    |  avio_read(pb, value, size);
91    |  if (!value[0]) {
    18←buffer read by avio_read may be partially uninitialized
92    |         av_free(value);
93    |  return;
94    |     }
95    |  
96    |     value[size] = 0;
97    |     av_dict_set(&avctx->metadata, tag, value, AV_DICT_DONT_STRDUP_VAL);
98    | }
99    |  
100   | static void read_uint8(AVFormatContext *avctx, AVIOContext *pb, const char *tag, const char *fmt)
101   | {
102   |     av_dict_set_int(&avctx->metadata, tag, avio_r8(pb), 0);
103   | }
104   |  
105   | static void read_uint16(AVFormatContext *avctx, AVIOContext *pb, const char *tag, const char *fmt)
106   | {
107   |     av_dict_set_int(&avctx->metadata, tag, avio_rl16(pb), 0);
108   | }
109   |  
110   | static void read_uint32(AVFormatContext *avctx, AVIOContext *pb, const char *tag, const char *fmt)
111   | {
112   |     av_dict_set_int(&avctx->metadata, tag, avio_rl32(pb), 0);
113   | }
114   |  
115   | static void read_uint64(AVFormatContext *avctx, AVIOContext *pb, const char *tag, const char *fmt)
116   | {
117   |     av_dict_set_int(&avctx->metadata, tag, avio_rl64(pb), 0);
118   | }
119   |  
120   | static int scan_file(AVFormatContext *avctx, AVStream *vst, AVStream *ast, int file)
121   | {
122   |  MlvContext *mlv = avctx->priv_data;
123   |     AVIOContext *pb = mlv->pb[file];
124   |  int ret;
125   |  while (!avio_feof(pb)) {
    7←Assuming the condition is true→
    8←Loop condition is true.  Entering loop body→
126   |  int type;
127   |  unsigned int size;
128   |         type = avio_rl32(pb);
129   |         size = avio_rl32(pb);
130   |         avio_skip(pb, 8); //timestamp
131   |  if (size < 16)
    9←Assuming 'size' is >= 16→
    10←Taking false branch→
132   |  break;
133   |  size -= 16;
134   |  if (vst10.1'vst' is null && type == MKTAG('R','A','W','I') && size >= 164) {
135   |  unsigned width  = avio_rl16(pb);
136   |  unsigned height = avio_rl16(pb);
137   |  unsigned bits_per_coded_sample;
138   |             ret = av_image_check_size(width, height, 0, avctx);
139   |  if (ret < 0)
140   |  return ret;
141   |  if (avio_rl32(pb) != 1)
142   |                 avpriv_request_sample(avctx, "raw api version");
143   |             avio_skip(pb, 20); // pointer, width, height, pitch, frame_size
144   |             bits_per_coded_sample = avio_rl32(pb);
145   |  if (bits_per_coded_sample > (INT_MAX - 7) / (width * height)) {
146   |                 av_log(avctx, AV_LOG_ERROR,
147   |  "invalid bits_per_coded_sample %u (size: %ux%u)\n",
148   |                        bits_per_coded_sample, width, height);
149   |  return AVERROR_INVALIDDATA;
150   |             }
151   |             vst->codecpar->width  = width;
152   |             vst->codecpar->height = height;
153   |             vst->codecpar->bits_per_coded_sample = bits_per_coded_sample;
154   |             avio_skip(pb, 8 + 16 + 24); // black_level, white_level, xywh, active_area, exposure_bias
155   |  if (avio_rl32(pb) != 0x2010100) /* RGGB */
156   |                 avpriv_request_sample(avctx, "cfa_pattern");
157   |             avio_skip(pb, 80); // calibration_illuminant1, color_matrix1, dynamic_range
158   |             vst->codecpar->format    = AV_PIX_FMT_BAYER_RGGB16LE;
159   |             vst->codecpar->codec_tag = MKTAG('B', 'I', 'T', 16);
160   |             size -= 164;
161   |         } else if (ast10.2'ast' is null && type == MKTAG('W', 'A', 'V', 'I') && size >= 16) {
162   |             ret = ff_get_wav_header(avctx, pb, ast->codecpar, 16, 0);
163   |  if (ret < 0)
164   |  return ret;
165   |             size -= 16;
166   |         } else if (type == MKTAG('I','N','F','O')) {
    11←Assuming the condition is true→
    12←Taking true branch→
167   |  if (size > 0)
    13←Assuming 'size' is > 0→
    14←Taking true branch→
168   |  read_string(avctx, pb, "info", size);
    15←Calling 'read_string'→
169   |  continue;
170   |         } else if (type == MKTAG('I','D','N','T') && size >= 36) {
171   |             read_string(avctx, pb, "cameraName", 32);
172   |             read_uint32(avctx, pb, "cameraModel", "0x%"PRIx32);
173   |             size -= 36;
174   |  if (size >= 32) {
175   |                 read_string(avctx, pb, "cameraSerial", 32);
176   |                 size -= 32;
177   |             }
178   |         } else if (type == MKTAG('L','E','N','S') && size >= 48) {
179   |             read_uint16(avctx, pb, "focalLength", "%i");
180   |             read_uint16(avctx, pb, "focalDist", "%i");
181   |             read_uint16(avctx, pb, "aperture", "%i");
182   |             read_uint8(avctx, pb, "stabilizerMode", "%i");
183   |             read_uint8(avctx, pb, "autofocusMode", "%i");
184   |             read_uint32(avctx, pb, "flags", "0x%"PRIx32);
185   |             read_uint32(avctx, pb, "lensID", "%"PRIi32);
186   |             read_string(avctx, pb, "lensName", 32);
187   |             size -= 48;
188   |  if (size >= 32) {
189   |                 read_string(avctx, pb, "lensSerial", 32);
190   |                 size -= 32;
191   |             }
192   |         } else if (vst && type == MKTAG('V', 'I', 'D', 'F') && size >= 4) {
193   |             uint64_t pts = avio_rl32(pb);
194   |             ff_add_index_entry(&vst->internal->index_entries, &vst->internal->nb_index_entries, &vst->internal->index_entries_allocated_size,
195   |                                avio_tell(pb) - 20, pts, file, 0, AVINDEX_KEYFRAME);
196   |             size -= 4;
197   |         } else if (ast && type == MKTAG('A', 'U', 'D', 'F') && size >= 4) {
198   |             uint64_t pts = avio_rl32(pb);
207   |             read_uint32(avctx, pb, "wbgain_b", "%"PRIi32);
208   |             read_uint32(avctx, pb, "wbs_gm", "%"PRIi32);
209   |             read_uint32(avctx, pb, "wbs_ba", "%"PRIi32);
210   |             size -= 28;
211   |         } else if (type == MKTAG('R','T','C','I') && size >= 20) {
212   |  char str[32];
213   |  struct tm time = { 0 };
214   |             time.tm_sec    = avio_rl16(pb);
215   |             time.tm_min    = avio_rl16(pb);
216   |             time.tm_hour   = avio_rl16(pb);
217   |             time.tm_mday   = avio_rl16(pb);
218   |             time.tm_mon    = avio_rl16(pb);
219   |             time.tm_year   = avio_rl16(pb);
220   |             time.tm_wday   = avio_rl16(pb);
221   |             time.tm_yday   = avio_rl16(pb);
222   |             time.tm_isdst  = avio_rl16(pb);
223   |             avio_skip(pb, 2);
224   |  if (strftime(str, sizeof(str), "%Y-%m-%d %H:%M:%S", &time))
225   |                 av_dict_set(&avctx->metadata, "time", str, 0);
226   |             size -= 20;
227   |         } else if (type == MKTAG('E','X','P','O') && size >= 16) {
228   |             av_dict_set(&avctx->metadata, "isoMode", avio_rl32(pb) ? "auto" : "manual", 0);
229   |             read_uint32(avctx, pb, "isoValue", "%"PRIi32);
230   |             read_uint32(avctx, pb, "isoAnalog", "%"PRIi32);
231   |             read_uint32(avctx, pb, "digitalGain", "%"PRIi32);
232   |             size -= 16;
233   |  if (size >= 8) {
234   |                 read_uint64(avctx, pb, "shutterValue", "%"PRIi64);
235   |                 size -= 8;
236   |             }
237   |         } else if (type == MKTAG('S','T','Y','L') && size >= 36) {
238   |             read_uint32(avctx, pb, "picStyleId", "%"PRIi32);
239   |             read_uint32(avctx, pb, "contrast", "%"PRIi32);
240   |             read_uint32(avctx, pb, "sharpness", "%"PRIi32);
241   |             read_uint32(avctx, pb, "saturation", "%"PRIi32);
242   |             read_uint32(avctx, pb, "colortone", "%"PRIi32);
243   |             read_string(avctx, pb, "picStyleName", 16);
244   |             size -= 36;
245   |         } else if (type == MKTAG('M','A','R','K')) {
246   |         } else if (type == MKTAG('N','U','L','L')) {
247   |         } else if (type == MKTAG('M','L','V','I')) { /* occurs when MLV and Mnn files are concatenated */
248   |         } else {
249   |             av_log(avctx, AV_LOG_INFO, "unsupported tag %s, size %u\n",
250   |  av_fourcc2str(type), size);
251   |         }
252   |         avio_skip(pb, size);
253   |     }
254   |  return 0;
255   | }
256   |  
257   | static int read_header(AVFormatContext *avctx)
258   | {
259   |  MlvContext *mlv = avctx->priv_data;
260   |     AVIOContext *pb = avctx->pb;
261   |     AVStream *vst = NULL, *ast = NULL;
262   |  int size, ret;
263   |  unsigned nb_video_frames, nb_audio_frames;
264   |     uint64_t guid;
265   |  char guidstr[32];
266   |  
267   |     avio_skip(pb, 4);
268   |     size = avio_rl32(pb);
269   |  if (size < 52)
    1Assuming 'size' is >= 52→
    2←Taking false branch→
270   |  return AVERROR_INVALIDDATA;
271   |  
272   |  avio_skip(pb, 8);
273   |  
274   |     guid = avio_rl64(pb);
275   |     snprintf(guidstr, sizeof(guidstr), "0x%"PRIx64, guid);
276   |     av_dict_set(&avctx->metadata, "guid", guidstr, 0);
277   |  
278   |     avio_skip(pb, 8); //fileNum, fileCount, fileFlags
279   |  
280   |     mlv->class[0] = avio_rl16(pb);
281   |     mlv->class[1] = avio_rl16(pb);
282   |  
283   |     nb_video_frames = avio_rl32(pb);
284   |     nb_audio_frames = avio_rl32(pb);
285   |  
286   |  if (nb_video_frames && mlv->class[0]) {
    3←Assuming 'nb_video_frames' is 0→
287   |         vst = avformat_new_stream(avctx, NULL);
288   |  if (!vst)
289   |  return AVERROR(ENOMEM);
290   |         vst->id = 0;
291   |         vst->nb_frames = nb_video_frames;
292   |  if ((mlv->class[0] & (MLV_CLASS_FLAG_DELTA|MLV_CLASS_FLAG_LZMA)))
293   |             avpriv_request_sample(avctx, "compression");
294   |         vst->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
295   |  switch (mlv->class[0] & ~(MLV_CLASS_FLAG_DELTA|MLV_CLASS_FLAG_LZMA)) {
296   |  case MLV_VIDEO_CLASS_RAW:
297   |             vst->codecpar->codec_id = AV_CODEC_ID_RAWVIDEO;
298   |  break;
299   |  case MLV_VIDEO_CLASS_YUV:
300   |             vst->codecpar->format   = AV_PIX_FMT_YUV420P;
301   |             vst->codecpar->codec_id = AV_CODEC_ID_RAWVIDEO;
302   |             vst->codecpar->codec_tag = 0;
303   |  break;
304   |  case MLV_VIDEO_CLASS_JPEG:
305   |             vst->codecpar->codec_id = AV_CODEC_ID_MJPEG;
306   |             vst->codecpar->codec_tag = 0;
307   |  break;
308   |  case MLV_VIDEO_CLASS_H264:
309   |             vst->codecpar->codec_id = AV_CODEC_ID_H264;
310   |             vst->codecpar->codec_tag = 0;
311   |  break;
312   |  default:
313   |             avpriv_request_sample(avctx, "unknown video class");
314   |         }
315   |     }
316   |  
317   |  if (nb_audio_frames && mlv->class[1]) {
    4←Assuming 'nb_audio_frames' is 0→
318   |         ast = avformat_new_stream(avctx, NULL);
319   |  if (!ast)
320   |  return AVERROR(ENOMEM);
321   |         ast->id = 1;
322   |         ast->nb_frames = nb_audio_frames;
323   |  if ((mlv->class[1] & MLV_CLASS_FLAG_LZMA))
324   |             avpriv_request_sample(avctx, "compression");
325   |  if ((mlv->class[1] & ~MLV_CLASS_FLAG_LZMA) != MLV_AUDIO_CLASS_WAV)
326   |             avpriv_request_sample(avctx, "unknown audio class");
327   |  
328   |         ast->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
329   |         avpriv_set_pts_info(ast, 33, 1, ast->codecpar->sample_rate);
330   |     }
331   |  
332   |  if (vst4.1'vst' is null) {
    5←Taking false branch→
333   |        AVRational framerate;
334   |        framerate.num = avio_rl32(pb);
335   |        framerate.den = avio_rl32(pb);
336   |        avpriv_set_pts_info(vst, 64, framerate.den, framerate.num);
337   |     } else
338   |  avio_skip(pb, 8);
339   |  
340   |  avio_skip(pb, size - 52);
341   |  
342   |  /* scan primary file */
343   |     mlv->pb[100] = avctx->pb;
344   |  ret = scan_file(avctx, vst, ast, 100);
    6←Calling 'scan_file'→
345   |  if (ret < 0)
346   |  return ret;
347   |  
348   |  /* scan secondary files */
349   |  if (strlen(avctx->url) > 2) {
350   |  int i;
351   |  char *filename = av_strdup(avctx->url);
352   |  
353   |  if (!filename)
354   |  return AVERROR(ENOMEM);
355   |  
356   |  for (i = 0; i < 100; i++) {
357   |             snprintf(filename + strlen(filename) - 2, 3, "%02d", i);
358   |  if (avctx->io_open(avctx, &mlv->pb[i], filename, AVIO_FLAG_READ, NULL) < 0)
359   |  break;
360   |  if (check_file_header(mlv->pb[i], guid) < 0) {
361   |                 av_log(avctx, AV_LOG_WARNING, "ignoring %s; bad format or guid mismatch\n", filename);
362   |                 ff_format_io_close(avctx, &mlv->pb[i]);
363   |  continue;
364   |             }
365   |             av_log(avctx, AV_LOG_INFO, "scanning %s\n", filename);
366   |             ret = scan_file(avctx, vst, ast, i);
367   |  if (ret < 0) {
368   |                 av_log(avctx, AV_LOG_WARNING, "ignoring %s; %s\n", filename, av_err2str(ret));
369   |                 ff_format_io_close(avctx, &mlv->pb[i]);
370   |  continue;
371   |             }
372   |         }
373   |         av_free(filename);
374   |     }