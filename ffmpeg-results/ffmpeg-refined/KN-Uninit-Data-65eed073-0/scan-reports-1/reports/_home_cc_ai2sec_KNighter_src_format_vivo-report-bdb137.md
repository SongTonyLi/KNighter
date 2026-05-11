### Report Summary

File:| format/vivo.c  
---|---  
Warning:| line 158, column 16  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


72    |  
73    |  return AVPROBE_SCORE_MAX;
74    | }
75    |  
76    | static int vivo_get_packet_header(AVFormatContext *s)
77    | {
78    |     VivoContext *vivo = s->priv_data;
79    |     AVIOContext *pb = s->pb;
80    |  unsigned c, get_length = 0;
81    |  
82    |  if (avio_feof(pb))
83    |  return AVERROR_EOF;
84    |  
85    |     c = avio_r8(pb);
86    |  if (c == 0x82) {
87    |         get_length = 1;
88    |         c = avio_r8(pb);
89    |     }
90    |  
91    |     vivo->type     = c >> 4;
92    |     vivo->sequence = c & 0xF;
93    |  
94    |  switch (vivo->type) {
95    |  case 0:   get_length =   1; break;
96    |  case 1: vivo->length = 128; break;
97    |  case 2:   get_length =   1; break;
98    |  case 3: vivo->length =  40; break;
99    |  case 4: vivo->length =  24; break;
100   |  default:
101   |         av_log(s, AV_LOG_ERROR, "unknown packet type %d\n", vivo->type);
102   |  return AVERROR_INVALIDDATA;
103   |     }
104   |  
105   |  if (get_length) {
106   |         c = avio_r8(pb);
107   |         vivo->length = c & 0x7F;
108   |  if (c & 0x80) {
109   |             c = avio_r8(pb);
110   |             vivo->length = (vivo->length << 7) | (c & 0x7F);
111   |  
112   |  if (c & 0x80) {
113   |                 av_log(s, AV_LOG_ERROR, "coded length is more than two bytes\n");
114   |  return AVERROR_INVALIDDATA;
115   |             }
116   |         }
117   |     }
118   |  
119   |  return 0;
120   | }
121   |  
122   | static int vivo_read_header(AVFormatContext *s)
123   | {
124   |  VivoContext *vivo = s->priv_data;
125   |     AVRational fps = { 0 };
126   |     AVStream *ast, *vst;
127   |  unsigned char *line, *line_end, *key, *value;
128   |  long value_int;
129   |  int ret, value_used;
130   |     int64_t duration = 0;
131   |  char *end_value;
132   |  
133   |     vst = avformat_new_stream(s, NULL);
134   |     ast = avformat_new_stream(s, NULL);
135   |  if (!ast || !vst)
    1Assuming 'ast' is non-null→
    2←Assuming 'vst' is non-null→
    3←Taking false branch→
136   |  return AVERROR(ENOMEM);
137   |  
138   |  ast->codecpar->sample_rate = 8000;
139   |  
140   |  while (1) {
    4←Loop condition is true.  Entering loop body→
141   |  if ((ret = vivo_get_packet_header(s)) < 0)
    5←Assuming the condition is false→
142   |  return ret;
143   |  
144   |  // done reading all text header packets?
145   |  if (vivo->sequence || vivo->type)
    6←Assuming field 'sequence' is 0→
    7←Assuming field 'type' is 0→
    8←Taking false branch→
146   |  break;
147   |  
148   |  if (vivo->length <= 1024) {
    9←Assuming field 'length' is <= 1024→
    10←Taking true branch→
149   |  avio_read(s->pb, vivo->text, vivo->length);
150   |  vivo->text[vivo->length] = 0;
151   |         } else {
152   |             av_log(s, AV_LOG_WARNING, "too big header, skipping\n");
153   |             avio_skip(s->pb, vivo->length);
154   |  continue;
155   |         }
156   |  
157   |  line = vivo->text;
158   |  while (*line) {
    11←buffer read by avio_read may be partially uninitialized
159   |             line_end = strstr(line, "\r\n");
160   |  if (!line_end)
161   |  break;
162   |  
163   |             *line_end = 0;
164   |             key = line;
165   |             line = line_end + 2; // skip \r\n
166   |  
167   |  if (line_end == key) // skip blank lines
168   |  continue;
169   |  
170   |             value = strchr(key, ':');
171   |  if (!value) {
172   |                 av_log(s, AV_LOG_WARNING, "missing colon in key:value pair '%s'\n",
173   |                        key);
174   |  continue;
175   |             }
176   |  
177   |             *value++ = 0;
178   |  
179   |             av_log(s, AV_LOG_DEBUG, "header: '%s' = '%s'\n", key, value);
180   |  
181   |             value_int = strtol(value, &end_value, 10);
182   |             value_used = 0;
183   |  if (*end_value == 0) { // valid integer
184   |                 av_log(s, AV_LOG_DEBUG, "got a valid integer (%ld)\n", value_int);
185   |                 value_used = 1;
186   |  if (!strcmp(key, "Duration")) {
187   |                     duration = value_int;
188   |                 } else if (!strcmp(key, "Width")) {