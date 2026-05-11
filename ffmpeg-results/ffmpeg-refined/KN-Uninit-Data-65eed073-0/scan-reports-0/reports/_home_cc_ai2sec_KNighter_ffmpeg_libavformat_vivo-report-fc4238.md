### Report Summary

File:| /home/cc/ai2sec/KNighter/ffmpeg/libavformat/vivo.c  
---|---  
Warning:| line 143, column 31  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


70    |  
71    |  return AVPROBE_SCORE_MAX;
72    | }
73    |  
74    | static int vivo_get_packet_header(AVFormatContext *s)
75    | {
76    |     VivoContext *vivo = s->priv_data;
77    |     AVIOContext *pb = s->pb;
78    |  unsigned c, get_length = 0;
79    |  
80    |  if (avio_feof(pb))
81    |  return AVERROR_EOF;
82    |  
83    |     c = avio_r8(pb);
84    |  if (c == 0x82) {
85    |         get_length = 1;
86    |         c = avio_r8(pb);
87    |     }
88    |  
89    |     vivo->type     = c >> 4;
90    |     vivo->sequence = c & 0xF;
91    |  
92    |  switch (vivo->type) {
93    |  case 0:   get_length =   1; break;
94    |  case 1: vivo->length = 128; break;
95    |  case 2:   get_length =   1; break;
96    |  case 3: vivo->length =  40; break;
97    |  case 4: vivo->length =  24; break;
98    |  default:
99    |         av_log(s, AV_LOG_ERROR, "unknown packet type %d\n", vivo->type);
100   |  return AVERROR_INVALIDDATA;
101   |     }
102   |  
103   |  if (get_length) {
104   |         c = avio_r8(pb);
105   |         vivo->length = c & 0x7F;
106   |  if (c & 0x80) {
107   |             c = avio_r8(pb);
108   |             vivo->length = (vivo->length << 7) | (c & 0x7F);
109   |  
110   |  if (c & 0x80) {
111   |                 av_log(s, AV_LOG_ERROR, "coded length is more than two bytes\n");
112   |  return AVERROR_INVALIDDATA;
113   |             }
114   |         }
115   |     }
116   |  
117   |  return 0;
118   | }
119   |  
120   | static int vivo_read_header(AVFormatContext *s)
121   | {
122   |  VivoContext *vivo = s->priv_data;
123   |     AVRational fps = { 1, 25};
124   |     AVStream *ast, *vst;
125   |  unsigned char *line, *line_end, *key, *value;
126   |  long value_int;
127   |  int ret, value_used;
128   |     int64_t duration = 0;
129   |  char *end_value;
130   |  
131   |     vst = avformat_new_stream(s, NULL);
132   |     ast = avformat_new_stream(s, NULL);
133   |  if (!ast || !vst)
    1Assuming 'ast' is non-null→
    2←Assuming 'vst' is non-null→
    3←Taking false branch→
134   |  return AVERROR(ENOMEM);
135   |  
136   |  ast->codecpar->sample_rate = 8000;
137   |  
138   |  while (1) {
    4←Loop condition is true.  Entering loop body→
    14← Execution continues on line 138→
    15←Loop condition is true.  Entering loop body→
139   |  if ((ret = vivo_get_packet_header(s)) < 0)
    5←Assuming the condition is false→
    16←Assuming the condition is false→
140   |  return ret;
141   |  
142   |  // done reading all text header packets?
143   |  if (vivo->sequence || vivo->type)
    6←Assuming field 'sequence' is 0→
    7←Assuming field 'type' is 0→
    8←Taking false branch→
    17←Assuming field 'sequence' is 0→
    18←buffer read by avio_read may be partially uninitialized
144   |  break;
145   |  
146   |  if (vivo->length <= 1024) {
    9←Assuming field 'length' is <= 1024→
    10←Taking true branch→
147   |  avio_read(s->pb, vivo->text, vivo->length);
148   |  vivo->text[vivo->length] = 0;
149   |         } else {
150   |             av_log(s, AV_LOG_WARNING, "too big header, skipping\n");
151   |             avio_skip(s->pb, vivo->length);
152   |  continue;
153   |         }
154   |  
155   |  line = vivo->text;
156   |  while (*line) {
    11←Loop condition is true.  Entering loop body→
157   |  line_end = strstr(line, "\r\n");
158   |  if (!line_end)
    12←Assuming 'line_end' is null→
    13←Taking true branch→
159   |  break;
160   |  
161   |             *line_end = 0;
162   |             key = line;
163   |             line = line_end + 2; // skip \r\n
164   |  
165   |  if (line_end == key) // skip blank lines
166   |  continue;
167   |  
168   |             value = strchr(key, ':');
169   |  if (!value) {
170   |                 av_log(s, AV_LOG_WARNING, "missing colon in key:value pair '%s'\n",
171   |                        key);
172   |  continue;
173   |             }
174   |  
175   |             *value++ = 0;
176   |  
177   |             av_log(s, AV_LOG_DEBUG, "header: '%s' = '%s'\n", key, value);
178   |  
179   |             value_int = strtol(value, &end_value, 10);
180   |             value_used = 0;
181   |  if (*end_value == 0) { // valid integer
182   |                 av_log(s, AV_LOG_DEBUG, "got a valid integer (%ld)\n", value_int);
183   |                 value_used = 1;
184   |  if (!strcmp(key, "Duration")) {
185   |                     duration = value_int;
186   |                 } else if (!strcmp(key, "Width")) {
187   |                     vst->codecpar->width = value_int;
188   |                 } else if (!strcmp(key, "Height")) {
189   |                     vst->codecpar->height = value_int;
190   |                 } else if (!strcmp(key, "TimeUnitNumerator")) {
191   |                     fps.num = value_int / 1000;
192   |                 } else if (!strcmp(key, "TimeUnitDenominator")) {
193   |                     fps.den = value_int;
194   |                 } else if (!strcmp(key, "SamplingFrequency")) {
195   |                     ast->codecpar->sample_rate = value_int;
196   |                 } else if (!strcmp(key, "NominalBitrate")) {
197   |                 } else if (!strcmp(key, "Length")) {
198   |  // size of file
199   |                 } else {
200   |                     value_used = 0;
201   |                 }
202   |             }
203   |  
204   |  if (!strcmp(key, "Version")) {
205   |  if (sscanf(value, "Vivo/%d.", &vivo->version) != 1)
206   |  return AVERROR_INVALIDDATA;
207   |                 value_used = 1;
208   |             } else if (!strcmp(key, "FPS")) {
209   |                 AVRational tmp;
210   |  
211   |                 value_used = 1;
212   |  if (!av_parse_ratio(&tmp, value, 10000, AV_LOG_WARNING, s))
213   |                     fps = av_inv_q(tmp);
214   |             }
215   |  
216   |  if (!value_used)
217   |                 av_dict_set(&s->metadata, key, value, 0);
218   |         }
219   |  }
220   |  
221   |     avpriv_set_pts_info(ast, 64, 1, ast->codecpar->sample_rate);
222   |     avpriv_set_pts_info(vst, 64, fps.num, fps.den);
223   |  if (duration)
224   |         s->duration = av_rescale(duration, 1000, 1);
225   |  
226   |     vst->start_time        = 0;
227   |     vst->codecpar->codec_tag  = 0;
228   |     vst->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
229   |  
230   |  if (vivo->version == 1) {
231   |         vst->codecpar->codec_id = AV_CODEC_ID_H263;
232   |         ast->codecpar->codec_id = AV_CODEC_ID_G723_1;
233   |         ast->codecpar->bits_per_coded_sample = 8;
234   |         ast->codecpar->block_align = 24;
235   |         ast->codecpar->bit_rate = 6400;
236   |     } else {
237   |         ast->codecpar->codec_id = AV_CODEC_ID_SIREN;
238   |         ast->codecpar->bits_per_coded_sample = 16;
239   |         ast->codecpar->block_align = 40;
240   |         ast->codecpar->bit_rate = 6400;
241   |         vivo->duration = 320;
242   |     }
243   |  
244   |     ast->start_time        = 0;
245   |     ast->codecpar->codec_tag  = 0;
246   |     ast->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
247   |     ast->codecpar->channels = 1;
248   |  
249   |  return 0;