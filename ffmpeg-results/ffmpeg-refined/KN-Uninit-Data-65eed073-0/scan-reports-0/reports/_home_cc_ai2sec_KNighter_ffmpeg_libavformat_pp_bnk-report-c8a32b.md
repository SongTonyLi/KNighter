### Report Summary

File:| /home/cc/ai2sec/KNighter/ffmpeg/libavformat/pp_bnk.c  
---|---  
Warning:| line 80, column 27  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


17    |  *
18    |  * You should have received a copy of the GNU Lesser General Public
19    |  * License along with FFmpeg; if not, write to the Free Software
20    |  * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
21    |  */
22    | #include "avformat.h"
23    | #include "internal.h"
24    | #include "libavutil/intreadwrite.h"
25    | #include "libavutil/avassert.h"
26    | #include "libavutil/internal.h"
27    |  
28    | #define PP_BNK_MAX_READ_SIZE    4096
29    | #define PP_BNK_FILE_HEADER_SIZE 20
30    | #define PP_BNK_TRACK_SIZE       20
31    |  
32    | typedef struct PPBnkHeader {
33    |     uint32_t        bank_id;        /*< Bank ID, useless for our purposes. */
34    |     uint32_t        sample_rate;    /*< Sample rate of the contained tracks. */
35    |     uint32_t        always1;        /*< Unknown, always seems to be 1. */
36    |     uint32_t        track_count;    /*< Number of tracks in the file. */
37    |     uint32_t        flags;          /*< Flags. */
38    | } PPBnkHeader;
39    |  
40    | typedef struct PPBnkTrack {
41    |     uint32_t        id;             /*< Track ID. Usually track[i].id == track[i-1].id + 1, but not always */
42    |     uint32_t        size;           /*< Size of the data in bytes. */
43    |     uint32_t        sample_rate;    /*< Sample rate. */
44    |     uint32_t        always1_1;      /*< Unknown, always seems to be 1. */
45    |     uint32_t        always1_2;      /*< Unknown, always seems to be 1. */
46    | } PPBnkTrack;
47    |  
48    | typedef struct PPBnkCtxTrack {
49    |     int64_t         data_offset;
50    |     uint32_t        data_size;
51    |     uint32_t        bytes_read;
52    | } PPBnkCtxTrack;
53    |  
54    | typedef struct PPBnkCtx {
55    |  int             track_count;
56    |     PPBnkCtxTrack   *tracks;
57    |     uint32_t        current_track;
58    |  int             is_music;
59    | } PPBnkCtx;
60    |  
61    | enum {
62    |     PP_BNK_FLAG_PERSIST = (1 << 0), /*< This is a large file, keep in memory. */
63    |     PP_BNK_FLAG_MUSIC   = (1 << 1), /*< This is music. */
64    |     PP_BNK_FLAG_MASK    = (PP_BNK_FLAG_PERSIST | PP_BNK_FLAG_MUSIC)
65    | };
66    |  
67    | static void pp_bnk_parse_header(PPBnkHeader *hdr, const uint8_t *buf)
68    | {
69    |     hdr->bank_id        = AV_RL32(buf +  0);
70    |     hdr->sample_rate    = AV_RL32(buf +  4);
71    |     hdr->always1        = AV_RL32(buf +  8);
72    |     hdr->track_count    = AV_RL32(buf + 12);
73    |     hdr->flags          = AV_RL32(buf + 16);
74    | }
75    |  
76    | static void pp_bnk_parse_track(PPBnkTrack *trk, const uint8_t *buf)
77    | {
78    |  trk->id             = AV_RL32(buf +  0);
79    |     trk->size           = AV_RL32(buf +  4);
80    |  trk->sample_rate    = AV_RL32(buf +  8);
    19←buffer read by avio_read may be partially uninitialized
81    |     trk->always1_1      = AV_RL32(buf + 12);
82    |     trk->always1_2      = AV_RL32(buf + 16);
83    | }
84    |  
85    | static int pp_bnk_probe(const AVProbeData *p)
86    | {
87    |     uint32_t sample_rate = AV_RL32(p->buf +  4);
88    |     uint32_t track_count = AV_RL32(p->buf + 12);
89    |     uint32_t flags       = AV_RL32(p->buf + 16);
90    |  
91    |  if (track_count == 0 || track_count > INT_MAX)
92    |  return 0;
93    |  
94    |  if ((sample_rate !=  5512) && (sample_rate != 11025) &&
95    |         (sample_rate != 22050) && (sample_rate != 44100))
96    |  return 0;
97    |  
98    |  /* Check the first track header. */
99    |  if (AV_RL32(p->buf + 28) != sample_rate)
100   |  return 0;
101   |  
102   |  if ((flags & ~PP_BNK_FLAG_MASK) != 0)
103   |  return 0;
104   |  
105   |  return AVPROBE_SCORE_MAX / 4 + 1;
106   | }
107   |  
108   | static int pp_bnk_read_header(AVFormatContext *s)
109   | {
110   |  int64_t ret;
111   |     AVStream *st;
112   |     AVCodecParameters *par;
113   |     PPBnkCtx *ctx = s->priv_data;
114   |     uint8_t buf[FFMAX(PP_BNK_FILE_HEADER_SIZE, PP_BNK_TRACK_SIZE)];
115   |     PPBnkHeader hdr;
116   |  
117   |  if ((ret = avio_read(s->pb, buf, PP_BNK_FILE_HEADER_SIZE)) < 0)
    1Assuming the condition is false→
    2←Taking false branch→
118   |  return ret;
119   |  else if (ret != PP_BNK_FILE_HEADER_SIZE)
    3←Assuming 'ret' is equal to PP_BNK_FILE_HEADER_SIZE→
    4←Taking false branch→
120   |  return AVERROR(EIO);
121   |  
122   |  pp_bnk_parse_header(&hdr, buf);
123   |  
124   |  if (hdr.track_count == 0 || hdr.track_count > INT_MAX)
    5←Assuming field 'track_count' is not equal to 0→
    6←Assuming field 'track_count' is <= INT_MAX→
125   |  return AVERROR_INVALIDDATA;
126   |  
127   |  if (hdr.sample_rate == 0 || hdr.sample_rate > INT_MAX)
    7←Assuming field 'sample_rate' is not equal to 0→
    8←Assuming field 'sample_rate' is <= INT_MAX→
    9←Taking false branch→
128   |  return AVERROR_INVALIDDATA;
129   |  
130   |  if (hdr.always1 != 1) {
    10←Assuming field 'always1' is equal to 1→
    11←Taking false branch→
131   |         avpriv_request_sample(s, "Non-one header value");
132   |  return AVERROR_PATCHWELCOME;
133   |     }
134   |  
135   |  ctx->track_count = hdr.track_count;
136   |  
137   |  if (!(ctx->tracks = av_malloc_array(hdr.track_count, sizeof(PPBnkCtxTrack))))
    12←Assuming field 'tracks' is non-null→
    13←Taking false branch→
138   |  return AVERROR(ENOMEM);
139   |  
140   |  /* Parse and validate each track. */
141   |  for (int i = 0; i13.1'i' is < field 'track_count' < hdr.track_count; i++) {
    14←Loop condition is true.  Entering loop body→
142   |  PPBnkTrack e;
143   |         PPBnkCtxTrack *trk = ctx->tracks + i;
144   |  
145   |         ret = avio_read(s->pb, buf, PP_BNK_TRACK_SIZE);
146   |  if (ret < 0 && ret != AVERROR_EOF)
    15←Assuming 'ret' is >= 0→
147   |  goto fail;
148   |  
149   |  /* Short byte-count or EOF, we have a truncated file. */
150   |  if (ret != PP_BNK_TRACK_SIZE) {
    16←Assuming 'ret' is equal to PP_BNK_TRACK_SIZE→
    17←Taking false branch→
151   |             av_log(s, AV_LOG_WARNING, "File truncated at %d/%u track(s)\n",
152   |                    i, hdr.track_count);
153   |             ctx->track_count = i;
154   |  break;
155   |         }
156   |  
157   |  pp_bnk_parse_track(&e, buf);
    18←Calling 'pp_bnk_parse_track'→
158   |  
159   |  /* The individual sample rates of all tracks must match that of the file header. */
160   |  if (e.sample_rate != hdr.sample_rate) {
161   |             ret = AVERROR_INVALIDDATA;
162   |  goto fail;
163   |         }
164   |  
165   |  if (e.always1_1 != 1 || e.always1_2 != 1) {
166   |             avpriv_request_sample(s, "Non-one track header values");
167   |             ret = AVERROR_PATCHWELCOME;
168   |  goto fail;
169   |         }
170   |  
171   |         trk->data_offset = avio_tell(s->pb);
172   |         trk->data_size   = e.size;
173   |         trk->bytes_read  = 0;
174   |  
175   |  /*
176   |  * Skip over the data to the next stream header.
177   |  * Sometimes avio_skip() doesn't detect EOF. If it doesn't, either:
178   |  *   - the avio_read() above will, or
179   |  *   - pp_bnk_read_packet() will read a truncated last track.
180   |  */
181   |  if ((ret = avio_skip(s->pb, e.size)) == AVERROR_EOF) {
182   |             ctx->track_count = i + 1;
183   |             av_log(s, AV_LOG_WARNING,
184   |  "Track %d has truncated data, assuming track count == %d\n",
185   |                    i, ctx->track_count);
186   |  break;
187   |         } else if (ret < 0) {