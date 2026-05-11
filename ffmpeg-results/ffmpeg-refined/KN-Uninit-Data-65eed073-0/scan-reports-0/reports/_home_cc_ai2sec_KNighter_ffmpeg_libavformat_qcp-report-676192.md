### Report Summary

File:| /home/cc/ai2sec/KNighter/ffmpeg/libavformat/qcp.c  
---|---  
Warning:| line 79, column 13  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


27    |  *     http://tools.ietf.org/html/rfc3625
28    |  */
29    |  
30    | #include "libavutil/channel_layout.h"
31    | #include "libavutil/intreadwrite.h"
32    | #include "avformat.h"
33    | #include "riff.h"
34    |  
35    | typedef struct QCPContext {
36    |     uint32_t data_size;                     ///< size of data chunk
37    |  
38    | #define QCP_MAX_MODE 4
39    |     int16_t rates_per_mode[QCP_MAX_MODE+1]; ///< contains the packet size corresponding
40    |  ///< to each mode, -1 if no size.
41    | } QCPContext;
42    |  
43    | /**
44    |  * Last 15 out of 16 bytes of QCELP-13K GUID, as stored in the file;
45    |  * the first byte of the GUID can be either 0x41 or 0x42.
46    |  */
47    | static const uint8_t guid_qcelp_13k_part[15] = {
48    |     0x6d, 0x7f, 0x5e, 0x15, 0xb1, 0xd0, 0x11, 0xba,
49    |     0x91, 0x00, 0x80, 0x5f, 0xb4, 0xb9, 0x7e
50    | };
51    |  
52    | /**
53    |  * EVRC GUID as stored in the file
54    |  */
55    | static const uint8_t guid_evrc[16] = {
56    |     0x8d, 0xd4, 0x89, 0xe6, 0x76, 0x90, 0xb5, 0x46,
57    |     0x91, 0xef, 0x73, 0x6a, 0x51, 0x00, 0xce, 0xb4
58    | };
59    |  
60    | static const uint8_t guid_4gv[16] = {
61    |     0xca, 0x29, 0xfd, 0x3c, 0x53, 0xf6, 0xf5, 0x4e,
62    |     0x90, 0xe9, 0xf4, 0x23, 0x6d, 0x59, 0x9b, 0x61
63    | };
64    |  
65    | /**
66    |  * SMV GUID as stored in the file
67    |  */
68    | static const uint8_t guid_smv[16] = {
69    |     0x75, 0x2b, 0x7c, 0x8d, 0x97, 0xa7, 0x49, 0xed,
70    |     0x98, 0x5e, 0xd5, 0x3c, 0x8c, 0xc7, 0x5f, 0x84
71    | };
72    |  
73    | /**
74    |  * @param guid contains at least 16 bytes
75    |  * @return 1 if the guid is a qcelp_13k guid, 0 otherwise
76    |  */
77    | static int is_qcelp_13k_guid(const uint8_t *guid) {
78    |  return (guid[0] == 0x41 || guid[0] == 0x42)
    4←Assuming the condition is true→
79    |         && !memcmp(guid+1, guid_qcelp_13k_part, sizeof(guid_qcelp_13k_part));
    5←buffer read by avio_read may be partially uninitialized
80    | }
81    |  
82    | static int qcp_probe(const AVProbeData *pd)
83    | {
84    |  if (AV_RL32(pd->buf  ) == AV_RL32("RIFF") &&
85    |  AV_RL64(pd->buf+8) == AV_RL64("QLCMfmt "))
86    |  return AVPROBE_SCORE_MAX;
87    |  return 0;
88    | }
89    |  
90    | static int qcp_read_header(AVFormatContext *s)
91    | {
92    |  AVIOContext *pb = s->pb;
93    |     QCPContext    *c  = s->priv_data;
94    |     AVStream      *st = avformat_new_stream(s, NULL);
95    |     uint8_t       buf[16];
96    |  int           i, nb_rates;
97    |  
98    |  if (!st)
    1Assuming 'st' is non-null→
    2←Taking false branch→
99    |  return AVERROR(ENOMEM);
100   |  
101   |  avio_rb32(pb);                    // "RIFF"
102   |     avio_skip(pb, 4 + 8 + 4 + 1 + 1);    // filesize + "QLCMfmt " + chunk-size + major-version + minor-version
103   |  
104   |     st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
105   |     st->codecpar->channels   = 1;
106   |     st->codecpar->channel_layout = AV_CH_LAYOUT_MONO;
107   |     avio_read(pb, buf, 16);
108   |  if (is_qcelp_13k_guid(buf)) {
    3←Calling 'is_qcelp_13k_guid'→
109   |         st->codecpar->codec_id = AV_CODEC_ID_QCELP;
110   |     } else if (!memcmp(buf, guid_evrc, 16)) {
111   |         st->codecpar->codec_id = AV_CODEC_ID_EVRC;
112   |     } else if (!memcmp(buf, guid_smv, 16)) {
113   |         st->codecpar->codec_id = AV_CODEC_ID_SMV;
114   |     } else if (!memcmp(buf, guid_4gv, 16)) {
115   |         st->codecpar->codec_id = AV_CODEC_ID_4GV;
116   |     } else {
117   |         av_log(s, AV_LOG_ERROR, "Unknown codec GUID "FF_PRI_GUID".\n",
118   |  FF_ARG_GUID(buf));
119   |  return AVERROR_INVALIDDATA;
120   |     }
121   |     avio_skip(pb, 2 + 80); // codec-version + codec-name
122   |     st->codecpar->bit_rate = avio_rl16(pb);
123   |  
124   |     s->packet_size = avio_rl16(pb);
125   |     avio_skip(pb, 2); // block-size
126   |     st->codecpar->sample_rate = avio_rl16(pb);
127   |     avio_skip(pb, 2); // sample-size
128   |  
129   |     memset(c->rates_per_mode, -1, sizeof(c->rates_per_mode));
130   |     nb_rates = avio_rl32(pb);
131   |     nb_rates = FFMIN(nb_rates, 8);
132   |  for (i=0; i<nb_rates; i++) {
133   |  int size = avio_r8(pb);
134   |  int mode = avio_r8(pb);
135   |  if (mode > QCP_MAX_MODE) {
136   |             av_log(s, AV_LOG_WARNING, "Unknown entry %d=>%d in rate-map-table.\n ", mode, size);
137   |         } else
138   |             c->rates_per_mode[mode] = size;