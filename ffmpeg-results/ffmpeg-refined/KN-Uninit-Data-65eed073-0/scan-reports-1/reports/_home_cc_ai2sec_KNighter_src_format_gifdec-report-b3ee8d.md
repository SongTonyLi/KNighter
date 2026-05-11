### Report Summary

File:| format/gifdec.c  
---|---  
Warning:| line 192, column 41  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


64    |  * is not explicitly set or have too low values. We assume default rate to be 10.
65    |  * Default delay = 100hundredths of second / 10fps = 10hos per frame.
66    |  */
67    | #define GIF_DEFAULT_DELAY   10
68    | /**
69    |  * By default delay values less than this threshold considered to be invalid.
70    |  */
71    | #define GIF_MIN_DELAY       2
72    |  
73    | static int gif_probe(const AVProbeData *p)
74    | {
75    |  /* check magick */
76    |  if (memcmp(p->buf, gif87a_sig, 6) && memcmp(p->buf, gif89a_sig, 6))
77    |  return 0;
78    |  
79    |  /* width or height contains zero? */
80    |  if (!AV_RL16(&p->buf[6]) || !AV_RL16(&p->buf[8]))
81    |  return 0;
82    |  
83    |  return AVPROBE_SCORE_MAX;
84    | }
85    |  
86    | static int resync(AVIOContext *pb)
87    | {
88    |  int ret = ffio_ensure_seekback(pb, 13);
89    |  if (ret < 0)
90    |  return ret;
91    |  
92    |  for (int i = 0; i < 6; i++) {
93    |  int b = avio_r8(pb);
94    |  if (b != gif87a_sig[i] && b != gif89a_sig[i])
95    |             i = -(b != 'G');
96    |  if (avio_feof(pb))
97    |  return AVERROR_EOF;
98    |     }
99    |  return 0;
100   | }
101   |  
102   | static int gif_skip_subblocks(AVIOContext *pb)
103   | {
104   |  int sb_size, ret = 0;
105   |  
106   |  while (0x00 != (sb_size = avio_r8(pb))) {
107   |  if ((ret = avio_skip(pb, sb_size)) < 0)
108   |  return ret;
109   |     }
110   |  
111   |  return ret;
112   | }
113   |  
114   | static int gif_read_header(AVFormatContext *s)
115   | {
116   |  GIFDemuxContext *gdc = s->priv_data;
117   |     AVIOContext     *pb  = s->pb;
118   |     AVStream        *st;
119   |  int type, width, height, ret, n, flags;
120   |     int64_t nb_frames = 0, duration = 0, pos;
121   |     int64_t ret64;
122   |  
123   |  if ((ret = resync(pb)) < 0)
    1Assuming the condition is false→
    2←Taking false branch→
124   |  return ret;
125   |  
126   |  pos = avio_tell(pb);
127   |     gdc->delay  = gdc->default_delay;
128   |     width  = avio_rl16(pb);
129   |     height = avio_rl16(pb);
130   |     flags = avio_r8(pb);
131   |     avio_skip(pb, 1);
132   |     n      = avio_r8(pb);
133   |  
134   |  if (width == 0 || height == 0)
    3←Assuming 'width' is not equal to 0→
    4←Assuming 'height' is not equal to 0→
    5←Taking false branch→
135   |  return AVERROR_INVALIDDATA;
136   |  
137   |  st = avformat_new_stream(s, NULL);
138   |  if (!st)
    6←Assuming 'st' is non-null→
    7←Taking false branch→
139   |  return AVERROR(ENOMEM);
140   |  
141   |  if (!(pb->seekable & AVIO_SEEKABLE_NORMAL))
    8←Assuming the condition is false→
    9←Taking false branch→
142   |  goto skip;
143   |  
144   |  if (flags & 0x80)
    10←Assuming the condition is false→
    11←Taking false branch→
145   |         avio_skip(pb, 3 * (1 << ((flags & 0x07) + 1)));
146   |  
147   |  while ((type = avio_r8(pb)) != GIF_TRAILER) {
    12←Assuming the condition is true→
    13←Loop condition is true.  Entering loop body→
148   |  if (avio_feof(pb))
    14←Assuming the condition is false→
    15←Taking false branch→
149   |  break;
150   |  if (type == GIF_EXTENSION_INTRODUCER) {
    16←Assuming 'type' is equal to GIF_EXTENSION_INTRODUCER→
    17←Taking true branch→
151   |  int subtype = avio_r8(pb);
152   |  if (subtype == GIF_COM_EXT_LABEL) {
    18←Assuming 'subtype' is not equal to GIF_COM_EXT_LABEL→
    19←Taking false branch→
153   |                 AVBPrint bp;
154   |  int block_size;
155   |  
156   |                 av_bprint_init(&bp, 0, AV_BPRINT_SIZE_UNLIMITED);
157   |  while ((block_size = avio_r8(pb)) != 0) {
158   |                     avio_read_to_bprint(pb, &bp, block_size);
159   |                 }
160   |                 av_dict_set(&s->metadata, "comment", bp.str, 0);
161   |                 av_bprint_finalize(&bp, NULL);
162   |             } else if (subtype == GIF_GCE_EXT_LABEL) {
    20←Assuming 'subtype' is not equal to GIF_GCE_EXT_LABEL→
    21←Taking false branch→
163   |  int block_size = avio_r8(pb);
164   |  
165   |  if (block_size == 4) {
166   |  int delay;
167   |  
168   |                     avio_skip(pb, 1);
169   |                     delay = avio_rl16(pb);
170   |                     delay = delay ? delay : gdc->default_delay;
171   |                     duration += delay;
172   |                     avio_skip(pb, 1);
173   |                 } else {
174   |                     avio_skip(pb, block_size);
175   |                 }
176   |                 gif_skip_subblocks(pb);
177   |             } else if (subtype == GIF_APP_EXT_LABEL) {
    22←Assuming 'subtype' is equal to GIF_APP_EXT_LABEL→
    23←Taking true branch→
178   |  uint8_t data[256];
179   |  int sb_size;
180   |  
181   |                 sb_size = avio_r8(pb);
182   |                 ret = avio_read(pb, data, sb_size);
183   |  if (ret < 0 || !sb_size)
    24←Assuming 'ret' is >= 0→
    25←Assuming 'sb_size' is not equal to 0→
    26←Taking false branch→
184   |  break;
185   |  
186   |  if (sb_size == strlen(NETSCAPE_EXT_STR)) {
    27←Assuming the condition is true→
    28←Taking true branch→
187   |  sb_size = avio_r8(pb);
188   |                     ret = avio_read(pb, data, sb_size);
189   |  if (ret < 0 || !sb_size)
    29←Assuming 'ret' is >= 0→
    30←Assuming 'sb_size' is not equal to 0→
190   |  break;
191   |  
192   |  if (sb_size == 3 && data[0] == 1) {
    31←Assuming 'sb_size' is equal to 3→
    32←buffer read by avio_read may be partially uninitialized
193   |                         gdc->total_iter = AV_RL16(data+1);
194   |                         av_log(s, AV_LOG_DEBUG, "Loop count is %d\n", gdc->total_iter);
195   |  
196   |  if (gdc->total_iter == 0)
197   |                             gdc->total_iter = -1;
198   |                     }
199   |                 }
200   |                 gif_skip_subblocks(pb);
201   |             } else {
202   |                 gif_skip_subblocks(pb);
203   |             }
204   |         } else if (type == GIF_IMAGE_SEPARATOR) {
205   |             avio_skip(pb, 8);
206   |             flags = avio_r8(pb);
207   |  if (flags & 0x80)
208   |                 avio_skip(pb, 3 * (1 << ((flags & 0x07) + 1)));
209   |             avio_skip(pb, 1);
210   |             gif_skip_subblocks(pb);
211   |             nb_frames++;
212   |         } else {
213   |  break;
214   |         }
215   |     }
216   |  
217   | skip:
218   |  /* jump to start because gif decoder needs header data too */
219   |     ret64 = avio_seek(pb, pos - 6, SEEK_SET);
220   |  if (ret64 < 0)
221   |  return (int)ret64;
222   |  