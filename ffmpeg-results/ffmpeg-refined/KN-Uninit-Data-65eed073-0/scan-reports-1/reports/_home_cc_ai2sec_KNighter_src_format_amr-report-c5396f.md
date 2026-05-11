### Report Summary

File:| format/amr.c  
---|---  
Warning:| line 105, column 17  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


19    |  * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
20    |  */
21    |  
22    | /*
23    | Write and read amr data according to RFC3267, http://www.ietf.org/rfc/rfc3267.txt?number=3267
24    | */
25    |  
26    | #include "config_components.h"
27    |  
28    | #include "libavutil/attributes_internal.h"
29    | #include "libavutil/channel_layout.h"
30    | #include "libavutil/intreadwrite.h"
31    | #include "avformat.h"
32    | #include "avio_internal.h"
33    | #include "demux.h"
34    | #include "internal.h"
35    | #include "mux.h"
36    | #include "rawdec.h"
37    | #include "rawenc.h"
38    |  
39    | typedef struct AMRContext {
40    |     FFRawDemuxerContext rawctx;
41    | } AMRContext;
42    |  
43    | static attribute_nonstring const uint8_t AMR_header[6]      = "#!AMR\x0a";
44    | static attribute_nonstring const uint8_t AMRMC_header[12]   = "#!AMR_MC1.0\x0a";
45    | static attribute_nonstring const uint8_t AMRWB_header[9]    = "#!AMR-WB\x0a";
46    | static attribute_nonstring const uint8_t AMRWBMC_header[15] = "#!AMR-WB_MC1.0\x0a";
47    |  
48    | static const uint8_t amrnb_packed_size[16] = {
49    |     13, 14, 16, 18, 20, 21, 27, 32, 6, 1, 1, 1, 1, 1, 1, 1
50    | };
51    | static const uint8_t amrwb_packed_size[16] = {
52    |     18, 24, 33, 37, 41, 47, 51, 59, 61, 6, 1, 1, 1, 1, 1, 1
53    | };
54    |  
55    | #if CONFIG_AMR_DEMUXER
56    | static int amr_probe(const AVProbeData *p)
57    | {
58    |  // Only check for "#!AMR" which could be amr-wb, amr-nb.
59    |  // This will also trigger multichannel files: "#!AMR_MC1.0\n" and
60    |  // "#!AMR-WB_MC1.0\n"
61    |  
62    |  if (!memcmp(p->buf, AMR_header, 5))
63    |  return AVPROBE_SCORE_MAX;
64    |  else
65    |  return 0;
66    | }
67    |  
68    | /* amr input */
69    | static int amr_read_header(AVFormatContext *s)
70    | {
71    |  AVIOContext *pb = s->pb;
72    |     AVStream *st;
73    |     uint8_t header[19] = { 0 };
74    |  int read, back = 0, ret;
75    |  
76    |     ret = ffio_ensure_seekback(s->pb, sizeof(header));
77    |  if (ret < 0)
    1Assuming 'ret' is >= 0→
    2←Taking false branch→
78    |  return ret;
79    |  
80    |  read = avio_read(pb, header, sizeof(header));
81    |  if (read < 0)
    3←Assuming 'read' is >= 0→
    4←Taking false branch→
82    |  return read;
83    |  
84    |  st = avformat_new_stream(s, NULL);
85    |  if (!st)
    5←Assuming 'st' is non-null→
    6←Taking false branch→
86    |  return AVERROR(ENOMEM);
87    |  if (!memcmp(header, AMR_header, sizeof(AMR_header))) {
    7←Assuming the condition is false→
    8←Taking false branch→
88    |         st->codecpar->codec_tag   = MKTAG('s', 'a', 'm', 'r');
89    |         st->codecpar->codec_id    = AV_CODEC_ID_AMR_NB;
90    |         st->codecpar->sample_rate = 8000;
91    |         st->codecpar->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
92    |         back = read - sizeof(AMR_header);
93    |     } else if (!memcmp(header, AMRWB_header, sizeof(AMRWB_header))) {
    9←Assuming the condition is false→
    10←Taking false branch→
94    |         st->codecpar->codec_tag   = MKTAG('s', 'a', 'w', 'b');
95    |         st->codecpar->codec_id    = AV_CODEC_ID_AMR_WB;
96    |         st->codecpar->sample_rate = 16000;
97    |         st->codecpar->ch_layout      = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
98    |         back = read - sizeof(AMRWB_header);
99    |     } else if (!memcmp(header, AMRMC_header, sizeof(AMRMC_header))) {
    11←Assuming the condition is false→
    12←Taking false branch→
100   |         st->codecpar->codec_tag   = MKTAG('s', 'a', 'm', 'r');
101   |         st->codecpar->codec_id    = AV_CODEC_ID_AMR_NB;
102   |         st->codecpar->sample_rate = 8000;
103   |         st->codecpar->ch_layout.nb_channels = AV_RL32(header + 12);
104   |         back = read - 4 - sizeof(AMRMC_header);
105   |     } else if (!memcmp(header, AMRWBMC_header, sizeof(AMRWBMC_header))) {
    13←buffer read by avio_read may be partially uninitialized
106   |         st->codecpar->codec_tag   = MKTAG('s', 'a', 'w', 'b');
107   |         st->codecpar->codec_id    = AV_CODEC_ID_AMR_WB;
108   |         st->codecpar->sample_rate = 16000;
109   |         st->codecpar->ch_layout.nb_channels = AV_RL32(header + 15);
110   |         back = read - 4 - sizeof(AMRWBMC_header);
111   |     } else {
112   |  return AVERROR_INVALIDDATA;
113   |     }
114   |  
115   |  if (st->codecpar->ch_layout.nb_channels < 1)
116   |  return AVERROR_INVALIDDATA;
117   |  
118   |     st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
119   |     ffstream(st)->need_parsing = AVSTREAM_PARSE_FULL_RAW;
120   |     avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);
121   |  
122   |  if (back > 0)
123   |         avio_seek(pb, -back, SEEK_CUR);
124   |  
125   |  return 0;
126   | }
127   |  
128   | const FFInputFormat ff_amr_demuxer = {
129   |     .p.name         = "amr",
130   |     .p.long_name    = NULL_IF_CONFIG_SMALL("3GPP AMR"),
131   |     .p.flags        = AVFMT_GENERIC_INDEX,
132   |     .p.priv_class   = &ff_raw_demuxer_class,
133   |     .priv_data_size = sizeof(AMRContext),
134   |     .read_probe     = amr_probe,
135   |     .read_header    = amr_read_header,