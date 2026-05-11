### Report Summary

File:| /home/cc/ai2sec/KNighter/ffmpeg/libavformat/mxg.c  
---|---  
Warning:| line 94, column 13  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


25    | #include "libavcodec/mjpeg.h"
26    | #include "avformat.h"
27    | #include "internal.h"
28    | #include "avio.h"
29    |  
30    | #define DEFAULT_PACKET_SIZE 1024
31    | #define OVERREAD_SIZE 3
32    |  
33    | typedef struct MXGContext {
34    |     uint8_t *buffer;
35    |     uint8_t *buffer_ptr;
36    |     uint8_t *soi_ptr;
37    |  unsigned int buffer_size;
38    |     int64_t dts;
39    |  unsigned int cache_size;
40    | } MXGContext;
41    |  
42    | static int mxg_read_header(AVFormatContext *s)
43    | {
44    |     AVStream *video_st, *audio_st;
45    |     MXGContext *mxg = s->priv_data;
46    |  
47    |  /* video parameters will be extracted from the compressed bitstream */
48    |     video_st = avformat_new_stream(s, NULL);
49    |  if (!video_st)
50    |  return AVERROR(ENOMEM);
51    |     video_st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
52    |     video_st->codecpar->codec_id = AV_CODEC_ID_MXPEG;
53    |     avpriv_set_pts_info(video_st, 64, 1, 1000000);
54    |  
55    |     audio_st = avformat_new_stream(s, NULL);
56    |  if (!audio_st)
57    |  return AVERROR(ENOMEM);
58    |     audio_st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
59    |     audio_st->codecpar->codec_id = AV_CODEC_ID_PCM_ALAW;
60    |     audio_st->codecpar->channels = 1;
61    |     audio_st->codecpar->channel_layout = AV_CH_LAYOUT_MONO;
62    |     audio_st->codecpar->sample_rate = 8000;
63    |     audio_st->codecpar->bits_per_coded_sample = 8;
64    |     audio_st->codecpar->block_align = 1;
65    |     avpriv_set_pts_info(audio_st, 64, 1, 1000000);
66    |  
67    |     mxg->soi_ptr = mxg->buffer_ptr = mxg->buffer = 0;
68    |     mxg->buffer_size = 0;
69    |     mxg->dts = AV_NOPTS_VALUE;
70    |     mxg->cache_size = 0;
71    |  
72    |  return 0;
73    | }
74    |  
75    | static uint8_t* mxg_find_startmarker(uint8_t *p, uint8_t *end)
76    | {
77    |  for (; p < end - 3; p += 4) {
    10←Assuming the condition is false→
    11←Loop condition is false. Execution continues on line 93→
78    |         uint32_t x = AV_RN32(p);
79    |  
80    |  if (x & (~(x+0x01010101)) & 0x80808080) {
81    |  if (p[0] == 0xff) {
82    |  return p;
83    |             } else if (p[1] == 0xff) {
84    |  return p+1;
85    |             } else if (p[2] == 0xff) {
86    |  return p+2;
87    |             } else if (p[3] == 0xff) {
88    |  return p+3;
89    |             }
90    |         }
91    |     }
92    |  
93    |  for (; p < end; ++p) {
    12←Assuming 'p' is < 'end'→
    13←Loop condition is true.  Entering loop body→
94    |  if (*p == 0xff) return p;
    14←buffer read by avio_read may be partially uninitialized
95    |     }
96    |  
97    |  return end;
98    | }
99    |  
100   | static int mxg_update_cache(AVFormatContext *s, unsigned int cache_size)
101   | {
102   |     MXGContext *mxg = s->priv_data;
103   |  unsigned int current_pos = mxg->buffer_ptr - mxg->buffer;
104   |  unsigned int soi_pos;
105   |     uint8_t *buffer;
106   |  int ret;
107   |  
108   |  /* reallocate internal buffer */
109   |  if (current_pos > current_pos + cache_size)
110   |  return AVERROR(ENOMEM);
111   |     soi_pos = mxg->soi_ptr - mxg->buffer;
112   |     buffer = av_fast_realloc(mxg->buffer, &mxg->buffer_size,
113   |                              current_pos + cache_size +
114   |  AV_INPUT_BUFFER_PADDING_SIZE);
115   |  if (!buffer)
116   |  return AVERROR(ENOMEM);
117   |     mxg->buffer = buffer;
118   |     mxg->buffer_ptr = mxg->buffer + current_pos;
119   |  if (mxg->soi_ptr) mxg->soi_ptr = mxg->buffer + soi_pos;
120   |  
121   |  /* get data */
122   |     ret = avio_read(s->pb, mxg->buffer_ptr + mxg->cache_size,
123   |                      cache_size - mxg->cache_size);
124   |  if (ret < 0)
125   |  return ret;
126   |  
127   |     mxg->cache_size += ret;
128   |  
129   |  return ret;
130   | }
131   |  
132   | static int mxg_read_packet(AVFormatContext *s, AVPacket *pkt)
133   | {
134   |  int ret;
135   |  unsigned int size;
136   |     uint8_t *startmarker_ptr, *end, *search_end, marker;
137   |     MXGContext *mxg = s->priv_data;
138   |  
139   |  while (!avio_feof(s->pb) && !s->pb->error){
    1Assuming the condition is true→
    2←Assuming field 'error' is 0→
    3←Loop condition is true.  Entering loop body→
140   |  if (mxg->cache_size <= OVERREAD_SIZE) {
    4←Assuming field 'cache_size' is <= OVERREAD_SIZE→
    5←Taking true branch→
141   |  /* update internal buffer */
142   |  ret = mxg_update_cache(s, DEFAULT_PACKET_SIZE + OVERREAD_SIZE);
143   |  if (ret5.1'ret' is >= 0 < 0)
    6←Taking false branch→
144   |  return ret;
145   |         }
146   |  end = mxg->buffer_ptr + mxg->cache_size;
147   |  
148   |  /* find start marker - 0xff */
149   |  if (mxg->cache_size > OVERREAD_SIZE) {
    7←Assuming field 'cache_size' is <= OVERREAD_SIZE→
    8←Taking false branch→
150   |             search_end = end - OVERREAD_SIZE;
151   |             startmarker_ptr = mxg_find_startmarker(mxg->buffer_ptr, search_end);
152   |         } else {
153   |  search_end = end;
154   |  startmarker_ptr = mxg_find_startmarker(mxg->buffer_ptr, search_end);
    9←Calling 'mxg_find_startmarker'→
155   |  if (startmarker_ptr >= search_end - 1 ||
156   |                 *(startmarker_ptr + 1) != EOI) break;
157   |         }
158   |  
159   |  if (startmarker_ptr != search_end) { /* start marker found */
160   |             marker = *(startmarker_ptr + 1);
161   |             mxg->buffer_ptr = startmarker_ptr + 2;
162   |             mxg->cache_size = end - mxg->buffer_ptr;
163   |  
164   |  if (marker == SOI) {
165   |                 mxg->soi_ptr = startmarker_ptr;
166   |             } else if (marker == EOI) {
167   |  if (!mxg->soi_ptr) {
168   |                     av_log(s, AV_LOG_WARNING, "Found EOI before SOI, skipping\n");
169   |  continue;
170   |                 }
171   |  
172   |                 size = mxg->buffer_ptr - mxg->soi_ptr;
173   |                 ret = av_new_packet(pkt, size);
174   |  if (ret < 0)
175   |  return ret;
176   |                 memcpy(pkt->data, mxg->soi_ptr, size);
177   |  
178   |                 pkt->pts = pkt->dts = mxg->dts;
179   |                 pkt->stream_index = 0;
180   |  
181   |  if (mxg->soi_ptr - mxg->buffer > mxg->cache_size) {
182   |  if (mxg->cache_size > 0) {
183   |                         memmove(mxg->buffer, mxg->buffer_ptr, mxg->cache_size);
184   |                     }