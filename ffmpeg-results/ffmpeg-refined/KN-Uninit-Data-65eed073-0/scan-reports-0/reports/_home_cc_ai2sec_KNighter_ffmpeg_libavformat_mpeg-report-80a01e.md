### Report Summary

File:| /home/cc/ai2sec/KNighter/ffmpeg/libavformat/mpeg.h  
---|---  
Warning:| line 71, column 14  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


32    |  int continue_pes;
33    | } PVAContext;
34    |  
35    | static int pva_check(const uint8_t *p) {
36    |  int length = AV_RB16(p + 6);
37    |  if (AV_RB16(p) != PVA_MAGIC || !p[2] || p[2] > 2 || p[4] != 0x55 ||
38    |         (p[5] & 0xe0) || length > PVA_MAX_PAYLOAD_LENGTH)
39    |  return -1;
40    |  return length + 8;
41    | }
42    |  
43    | static int pva_probe(const AVProbeData * pd) {
44    |  const unsigned char *buf = pd->buf;
45    |  int len = pva_check(buf);
46    |  
47    |  if (len < 0)
48    |  return 0;
49    |  
50    |  if (pd->buf_size >= len + 8 &&
51    |         pva_check(buf + len) >= 0)
52    |  return AVPROBE_SCORE_EXTENSION;
53    |  
54    |  return AVPROBE_SCORE_MAX / 4;
55    | }
56    |  
57    | static int pva_read_header(AVFormatContext *s) {
58    |     AVStream *st;
59    |  
60    |  if (!(st = avformat_new_stream(s, NULL)))
61    |  return AVERROR(ENOMEM);
62    |     st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
63    |     st->codecpar->codec_id   = AV_CODEC_ID_MPEG2VIDEO;
64    |     st->internal->need_parsing      = AVSTREAM_PARSE_FULL;
65    |     avpriv_set_pts_info(st, 32, 1, 90000);
66    |     av_add_index_entry(st, 0, 0, 0, 0, AVINDEX_KEYFRAME);
67    |  
68    |  if (!(st = avformat_new_stream(s, NULL)))
69    |  return AVERROR(ENOMEM);
70    |     st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
71    |     st->codecpar->codec_id   = AV_CODEC_ID_MP2;
72    |     st->internal->need_parsing      = AVSTREAM_PARSE_FULL;
73    |     avpriv_set_pts_info(st, 33, 1, 90000);
74    |     av_add_index_entry(st, 0, 0, 0, 0, AVINDEX_KEYFRAME);
75    |  
76    |  /* the parameters will be extracted from the compressed bitstream */
77    |  return 0;
78    | }
79    |  
80    | #define pva_log if (read_packet) av_log
81    |  
82    | static int read_part_of_packet(AVFormatContext *s, int64_t *pts,
83    |  int *len, int *strid, int read_packet) {
84    |  AVIOContext *pb = s->pb;
85    |     PVAContext *pvactx = s->priv_data;
86    |  int syncword, streamid, reserved, flags, length, pts_flag;
87    |     int64_t pva_pts = AV_NOPTS_VALUE, startpos;
88    |  int ret;
89    |  
90    | recover:
91    |  startpos = avio_tell(pb);
92    |  
93    |  syncword = avio_rb16(pb);
94    |     streamid = avio_r8(pb);
95    |     avio_r8(pb);               /* counter not used */
96    |     reserved = avio_r8(pb);
97    |     flags    = avio_r8(pb);
98    |     length   = avio_rb16(pb);
99    |  
100   |     pts_flag = flags & 0x10;
101   |  
102   |  if (syncword != PVA_MAGIC) {
    6←Assuming the condition is false→
103   |  pva_log(s, AV_LOG_ERROR, "invalid syncword\n");
104   |  return AVERROR(EIO);
105   |     }
106   |  if (streamid != PVA_VIDEO_PAYLOAD && streamid != PVA_AUDIO_PAYLOAD) {
    7←Assuming 'streamid' is not equal to PVA_VIDEO_PAYLOAD→
    8←Assuming 'streamid' is equal to PVA_AUDIO_PAYLOAD→
    9←Taking false branch→
107   |  pva_log(s, AV_LOG_ERROR, "invalid streamid\n");
108   |  return AVERROR(EIO);
109   |     }
110   |  if (reserved != 0x55) {
    10←Assuming 'reserved' is equal to 85→
    11←Taking false branch→
111   |  pva_log(s, AV_LOG_WARNING, "expected reserved byte to be 0x55\n");
112   |     }
113   |  if (length > PVA_MAX_PAYLOAD_LENGTH) {
    12←Assuming 'length' is <= PVA_MAX_PAYLOAD_LENGTH→
114   |  pva_log(s, AV_LOG_ERROR, "invalid payload length %u\n", length);
115   |  return AVERROR(EIO);
116   |     }
117   |  
118   |  if (streamid12.1'streamid' is not equal to PVA_VIDEO_PAYLOAD12.1'streamid' is not equal to PVA_VIDEO_PAYLOAD == PVA_VIDEO_PAYLOAD && pts_flag) {
119   |         pva_pts = avio_rb32(pb);
120   |         length -= 4;
121   |     } else if (streamid12.2'streamid' is equal to PVA_AUDIO_PAYLOAD12.2'streamid' is equal to PVA_AUDIO_PAYLOAD == PVA_AUDIO_PAYLOAD) {
    13←Taking true branch→
122   |  /* PVA Audio Packets either start with a signaled PES packet or
123   |  * are a continuation of the previous PES packet. New PES packets
124   |  * always start at the beginning of a PVA Packet, never somewhere in
125   |  * the middle. */
126   |  if (!pvactx->continue_pes13.1Field 'continue_pes' is 013.1Field 'continue_pes' is 0) {
    14←Taking true branch→
127   |  int pes_signal, pes_header_data_length, pes_packet_length,
128   |                 pes_flags;
129   |  unsigned char pes_header_data[256];
130   |  
131   |             pes_signal             = avio_rb24(pb);
132   |             avio_r8(pb);
133   |             pes_packet_length      = avio_rb16(pb);
134   |             pes_flags              = avio_rb16(pb);
135   |             pes_header_data_length = avio_r8(pb);
136   |  
137   |  if (avio_feof(pb)) {
    15←Assuming the condition is false→
138   |  return AVERROR_EOF;
139   |             }
140   |  
141   |  if (pes_signal != 1 || pes_header_data_length == 0) {
    16←Assuming 'pes_signal' is equal to 1→
    17←Assuming 'pes_header_data_length' is not equal to 0→
    18←Taking false branch→
142   |  pva_log(s, AV_LOG_WARNING, "expected non empty signaled PES packet, "
143   |  "trying to recover\n");
144   |                 avio_skip(pb, length - 9);
145   |  if (!read_packet)
146   |  return AVERROR(EIO);
147   |  goto recover;
148   |             }
149   |  
150   |  ret = avio_read(pb, pes_header_data, pes_header_data_length);
151   |  if (ret != pes_header_data_length)
    19←Assuming 'ret' is equal to 'pes_header_data_length'→
    20←Taking false branch→
152   |  return ret < 0 ? ret : AVERROR_INVALIDDATA;
153   |  length -= 9 + pes_header_data_length;
154   |  
155   |             pes_packet_length -= 3 + pes_header_data_length;
156   |  
157   |             pvactx->continue_pes = pes_packet_length;
158   |  
159   |  if (pes_flags & 0x80 && (pes_header_data[0] & 0xf0) == 0x20) {
    21←Assuming the condition is true→
    22←Assuming the condition is true→
    23←Taking true branch→
160   |  if (pes_header_data_length < 5) {
    24←Assuming 'pes_header_data_length' is >= 5→
    25←Taking false branch→
161   |  pva_log(s, AV_LOG_ERROR, "header too short\n");
162   |                     avio_skip(pb, length);
163   |  return AVERROR_INVALIDDATA;
164   |                 }
165   |  pva_pts = ff_parse_pes_pts(pes_header_data);
    26←Calling 'ff_parse_pes_pts'→
166   |             }
167   |         }
168   |  
169   |         pvactx->continue_pes -= length;
170   |  
171   |  if (pvactx->continue_pes < 0) {
172   |  pva_log(s, AV_LOG_WARNING, "audio data corruption\n");
173   |             pvactx->continue_pes = 0;
174   |         }
175   |     }
176   |  
177   |  if (pva_pts != AV_NOPTS_VALUE)
178   |         av_add_index_entry(s->streams[streamid-1], startpos, pva_pts, 0, 0, AVINDEX_KEYFRAME);
179   |  
180   |     *pts   = pva_pts;
181   |     *len   = length;
182   |     *strid = streamid;
183   |  return 0;
184   | }
185   |  
186   | static int pva_read_packet(AVFormatContext *s, AVPacket *pkt) {
187   |     AVIOContext *pb = s->pb;
188   |     int64_t pva_pts;
189   |  int ret, length, streamid;
190   |  
191   |  if (read_part_of_packet(s, &pva_pts, &length, &streamid, 1) < 0 ||
192   |        (ret = av_get_packet(pb, pkt, length)) <= 0)
193   |  return AVERROR(EIO);
194   |  
195   |     pkt->stream_index = streamid - 1;
196   |     pkt->pts = pva_pts;
197   |  
198   |  return ret;
199   | }
200   |  
201   | static int64_t pva_read_timestamp(struct AVFormatContext *s, int stream_index,
202   |                                           int64_t *pos, int64_t pos_limit) {
203   |  AVIOContext *pb = s->pb;
204   |     PVAContext *pvactx = s->priv_data;
205   |  int length, streamid;
206   |     int64_t res = AV_NOPTS_VALUE;
207   |  
208   |  pos_limit = FFMIN(*pos+PVA_MAX_PAYLOAD_LENGTH*8, (uint64_t)*pos+pos_limit);
    1Assuming the condition is false→
    2←'?' condition is false→
209   |  
210   |  while (*pos < pos_limit) {
    3←Assuming the condition is true→
    4←Loop condition is true.  Entering loop body→
211   |  res = AV_NOPTS_VALUE;
212   |         avio_seek(pb, *pos, SEEK_SET);
213   |  
214   |         pvactx->continue_pes = 0;
215   |  if (read_part_of_packet(s, &res, &length, &streamid, 0)) {
    5←Calling 'read_part_of_packet'→
216   |             (*pos)++;
217   |  continue;
218   |         }
219   |  if (streamid - 1 != stream_index || res == AV_NOPTS_VALUE) {
220   |             *pos = avio_tell(pb) + length;
221   |  continue;
222   |         }
223   |  break;
224   |     }
225   |  
226   |     pvactx->continue_pes = 0;
227   |  return res;
228   | }
229   |  
230   | const AVInputFormat ff_pva_demuxer = {
231   |     .name           = "pva",
232   |     .long_name      = NULL_IF_CONFIG_SMALL("TechnoTrend PVA"),
233   |     .priv_data_size = sizeof(PVAContext),
234   |     .read_probe     = pva_probe,
235   |     .read_header    = pva_read_header,
236   |     .read_packet    = pva_read_packet,
237   |     .read_timestamp = pva_read_timestamp,
238   | };
18    |  * License along with FFmpeg; if not, write to the Free Software
19    |  * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
20    |  */
21    |  
22    | #ifndef AVFORMAT_MPEG_H
23    | #define AVFORMAT_MPEG_H
24    |  
25    | #include <stdint.h>
26    | #include "libavutil/intreadwrite.h"
27    |  
28    | #define PACK_START_CODE             ((unsigned int)0x000001ba)
29    | #define SYSTEM_HEADER_START_CODE    ((unsigned int)0x000001bb)
30    | #define SEQUENCE_END_CODE           ((unsigned int)0x000001b7)
31    | #define PACKET_START_CODE_MASK      ((unsigned int)0xffffff00)
32    | #define PACKET_START_CODE_PREFIX    ((unsigned int)0x00000100)
33    | #define ISO_11172_END_CODE          ((unsigned int)0x000001b9)
34    |  
35    | /* mpeg2 */
36    | #define PROGRAM_STREAM_MAP 0x1bc
37    | #define PRIVATE_STREAM_1   0x1bd
38    | #define PADDING_STREAM     0x1be
39    | #define PRIVATE_STREAM_2   0x1bf
40    |  
41    | #define AUDIO_ID 0xc0
42    | #define VIDEO_ID 0xe0
43    | #define H264_ID  0xe2
44    | #define AC3_ID   0x80
45    | #define DTS_ID   0x88
46    | #define LPCM_ID  0xa0
47    | #define SUB_ID   0x20
48    |  
49    | #define STREAM_TYPE_VIDEO_MPEG1     0x01
50    | #define STREAM_TYPE_VIDEO_MPEG2     0x02
51    | #define STREAM_TYPE_AUDIO_MPEG1     0x03
52    | #define STREAM_TYPE_AUDIO_MPEG2     0x04
53    | #define STREAM_TYPE_PRIVATE_SECTION 0x05
54    | #define STREAM_TYPE_PRIVATE_DATA    0x06
55    | #define STREAM_TYPE_AUDIO_AAC       0x0f
56    | #define STREAM_TYPE_VIDEO_MPEG4     0x10
57    | #define STREAM_TYPE_VIDEO_H264      0x1b
58    | #define STREAM_TYPE_VIDEO_HEVC      0x24
59    | #define STREAM_TYPE_VIDEO_CAVS      0x42
60    |  
61    | #define STREAM_TYPE_AUDIO_AC3       0x81
62    |  
63    | static const int lpcm_freq_tab[4] = { 48000, 96000, 44100, 32000 };
64    |  
65    | /**
66    |  * Parse MPEG-PES five-byte timestamp
67    |  */
68    | static inline int64_t ff_parse_pes_pts(const uint8_t *buf) {
69    |  return (int64_t)(*buf & 0x0e) << 29 |
70    |             (AV_RB16(buf+1) >> 1) << 15 |
71    |  AV_RB16(buf+3) >> 1;
    27←buffer read by avio_read may be partially uninitialized
72    | }
73    |  
74    | #endif /* AVFORMAT_MPEG_H */