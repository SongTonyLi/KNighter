### Report Summary

File:| format/thp.c  
---|---  
Warning:| line 134, column 20  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


14    |  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
15    |  * Lesser General Public License for more details.
16    |  *
17    |  * You should have received a copy of the GNU Lesser General Public
18    |  * License along with FFmpeg; if not, write to the Free Software
19    |  * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
20    |  */
21    |  
22    | #include "libavutil/intreadwrite.h"
23    | #include "libavutil/intfloat.h"
24    | #include "avformat.h"
25    | #include "avio_internal.h"
26    | #include "demux.h"
27    | #include "internal.h"
28    |  
29    | typedef struct ThpDemuxContext {
30    |  int              version;
31    |  unsigned         first_frame;
32    |  unsigned         first_framesz;
33    |  unsigned         last_frame;
34    |  int              compoff;
35    |  unsigned         framecnt;
36    |     AVRational       fps;
37    |  unsigned         frame;
38    |     int64_t          next_frame;
39    |  unsigned         next_framesz;
40    |  int              video_stream_index;
41    |  int              audio_stream_index;
42    |  int              compcount;
43    |  unsigned char    components[16];
44    |     AVStream*        vst;
45    |  int              has_audio;
46    |  unsigned         audiosize;
47    | } ThpDemuxContext;
48    |  
49    |  
50    | static int thp_probe(const AVProbeData *p)
51    | {
52    |  double d;
53    |  /* check file header */
54    |  if (AV_RL32(p->buf) != MKTAG('T', 'H', 'P', '\0'))
55    |  return 0;
56    |  
57    |     d = av_int2float(AV_RB32(p->buf + 16));
58    |  if (d < 0.1 || d > 1000 || isnan(d))
59    |  return AVPROBE_SCORE_MAX/4;
60    |  
61    |  return AVPROBE_SCORE_MAX;
62    | }
63    |  
64    | static int thp_read_header(AVFormatContext *s)
65    | {
66    |  ThpDemuxContext *thp = s->priv_data;
67    |     AVStream *st;
68    |     AVIOContext *pb = s->pb;
69    |     int64_t fsize= avio_size(pb);
70    |     uint32_t maxsize;
71    |  int i;
72    |  
73    |  /* Read the file header.  */
74    |                            avio_rb32(pb); /* Skip Magic.  */
75    |     thp->version         = avio_rb32(pb);
76    |  
77    |                            avio_rb32(pb); /* Max buf size.  */
78    |                            avio_rb32(pb); /* Max samples.  */
79    |  
80    |     thp->fps             = av_d2q(av_int2float(avio_rb32(pb)), INT_MAX);
81    |  if (thp->fps.den <= 0 || thp->fps.num < 0)
    1Assuming field 'den' is > 0→
    2←Assuming field 'num' is >= 0→
    3←Taking false branch→
82    |  return AVERROR_INVALIDDATA;
83    |  thp->framecnt        = avio_rb32(pb);
84    |     thp->first_framesz   = avio_rb32(pb);
85    |     maxsize              = avio_rb32(pb);
86    |  if (fsize > 0 && (!maxsize || fsize < maxsize))
    4←Assuming 'fsize' is <= 0→
87    |         maxsize = fsize;
88    |  ffiocontext(pb)->maxsize = fsize;
89    |  
90    |     thp->compoff         = avio_rb32(pb);
91    |                            avio_rb32(pb); /* offsetDataOffset.  */
92    |     thp->first_frame     = avio_rb32(pb);
93    |     thp->last_frame      = avio_rb32(pb);
94    |  
95    |     thp->next_framesz    = thp->first_framesz;
96    |     thp->next_frame      = thp->first_frame;
97    |  
98    |  /* Read the component structure.  */
99    |     avio_seek (pb, thp->compoff, SEEK_SET);
100   |     thp->compcount       = avio_rb32(pb);
101   |  
102   |  if (thp->compcount > FF_ARRAY_ELEMS(thp->components))
    5←Assuming the condition is false→
    6←Taking false branch→
103   |  return AVERROR_INVALIDDATA;
104   |  
105   |  /* Read the list of component types.  */
106   |  avio_read(pb, thp->components, 16);
107   |  
108   |  for (i = 0; i < thp->compcount; i++) {
    7←Assuming 'i' is < field 'compcount'→
    8←Loop condition is true.  Entering loop body→
109   |  if (thp->components[i] == 0) {
    9←Assuming the condition is false→
    10←Taking false branch→
110   |  if (thp->vst)
111   |  break;
112   |  
113   |  /* Video component.  */
114   |             st = avformat_new_stream(s, NULL);
115   |  if (!st)
116   |  return AVERROR(ENOMEM);
117   |  
118   |  /* The denominator and numerator are switched because 1/fps
119   |  is required.  */
120   |             avpriv_set_pts_info(st, 64, thp->fps.den, thp->fps.num);
121   |             st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
122   |             st->codecpar->codec_id = AV_CODEC_ID_THP;
123   |             st->codecpar->codec_tag = 0;  /* no fourcc */
124   |             st->codecpar->width = avio_rb32(pb);
125   |             st->codecpar->height = avio_rb32(pb);
126   |             st->codecpar->sample_rate = av_q2d(thp->fps);
127   |             st->nb_frames =
128   |             st->duration = thp->framecnt;
129   |             thp->vst = st;
130   |             thp->video_stream_index = st->index;
131   |  
132   |  if (thp->version == 0x11000)
133   |                 avio_rb32(pb); /* Unknown.  */
134   |         } else if (thp->components[i] == 1) {
    11←buffer read by avio_read may be partially uninitialized
135   |  if (thp->has_audio != 0)
136   |  break;
137   |  
138   |  /* Audio component.  */
139   |             st = avformat_new_stream(s, NULL);
140   |  if (!st)
141   |  return AVERROR(ENOMEM);
142   |  
143   |             st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
144   |             st->codecpar->codec_id = AV_CODEC_ID_ADPCM_THP;
145   |             st->codecpar->codec_tag = 0;  /* no fourcc */
146   |             st->codecpar->ch_layout.nb_channels = avio_rb32(pb);
147   |             st->codecpar->sample_rate = avio_rb32(pb); /* Frequency.  */
148   |             st->duration           = avio_rb32(pb);
149   |  
150   |             avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);
151   |  
152   |             thp->audio_stream_index = st->index;
153   |             thp->has_audio = 1;
154   |         }
155   |     }
156   |  
157   |  if (!thp->vst)
158   |  return AVERROR_INVALIDDATA;
159   |  
160   |  return 0;
161   | }
162   |  
163   | static int thp_read_packet(AVFormatContext *s,
164   |                             AVPacket *pkt)