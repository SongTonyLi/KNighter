### Report Summary

File:| /home/cc/ai2sec/KNighter/ffmpeg/libavformat/vivo.c  
---|---  
Warning:| line 148, column 24  
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
139   |  if ((ret = vivo_get_packet_header(s)) < 0)
    5←Assuming the condition is false→
140   |  return ret;
141   |  
142   |  // done reading all text header packets?
143   |  if (vivo->sequence || vivo->type)
    6←Assuming field 'sequence' is 0→
    7←Assuming field 'type' is 0→
    8←Taking false branch→
144   |  break;
145   |  
146   |  if (vivo->length <= 1024) {
    9←Assuming field 'length' is <= 1024→
    10←Taking true branch→
147   |  avio_read(s->pb, vivo->text, vivo->length);
148   |  vivo->text[vivo->length] = 0;
    11←buffer read by avio_read may be partially uninitialized
149   |         } else {
150   |             av_log(s, AV_LOG_WARNING, "too big header, skipping\n");
151   |             avio_skip(s->pb, vivo->length);
152   |  continue;
153   |         }
154   |  
155   |         line = vivo->text;
156   |  while (*line) {
157   |             line_end = strstr(line, "\r\n");
158   |  if (!line_end)
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