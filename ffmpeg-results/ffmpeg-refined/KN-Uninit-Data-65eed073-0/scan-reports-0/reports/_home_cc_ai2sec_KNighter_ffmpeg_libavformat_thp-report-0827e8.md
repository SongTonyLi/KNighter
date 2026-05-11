### Report Summary

File:| /home/cc/ai2sec/KNighter/ffmpeg/libavformat/thp.c  
---|---  
Warning:| line 130, column 20  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


12    |  * FFmpeg is distributed in the hope that it will be useful,
13    |  * but WITHOUT ANY WARRANTY; without even the implied warranty of
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
25    | #include "internal.h"
26    |  
27    | typedef struct ThpDemuxContext {
28    |  int              version;
29    |  unsigned         first_frame;
30    |  unsigned         first_framesz;
31    |  unsigned         last_frame;
32    |  int              compoff;
33    |  unsigned         framecnt;
34    |     AVRational       fps;
35    |  unsigned         frame;
36    |     int64_t          next_frame;
37    |  unsigned         next_framesz;
38    |  int              video_stream_index;
39    |  int              audio_stream_index;
40    |  int              compcount;
41    |  unsigned char    components[16];
42    |     AVStream*        vst;
43    |  int              has_audio;
44    |  unsigned         audiosize;
45    | } ThpDemuxContext;
46    |  
47    |  
48    | static int thp_probe(const AVProbeData *p)
49    | {
50    |  double d;
51    |  /* check file header */
52    |  if (AV_RL32(p->buf) != MKTAG('T', 'H', 'P', '\0'))
53    |  return 0;
54    |  
55    |     d = av_int2float(AV_RB32(p->buf + 16));
56    |  if (d < 0.1 || d > 1000 || isnan(d))
57    |  return AVPROBE_SCORE_MAX/4;
58    |  
59    |  return AVPROBE_SCORE_MAX;
60    | }
61    |  
62    | static int thp_read_header(AVFormatContext *s)
63    | {
64    |  ThpDemuxContext *thp = s->priv_data;
65    |     AVStream *st;
66    |     AVIOContext *pb = s->pb;
67    |     int64_t fsize= avio_size(pb);
68    |  int i;
69    |  
70    |  /* Read the file header.  */
71    |                            avio_rb32(pb); /* Skip Magic.  */
72    |     thp->version         = avio_rb32(pb);
73    |  
74    |                            avio_rb32(pb); /* Max buf size.  */
75    |                            avio_rb32(pb); /* Max samples.  */
76    |  
77    |     thp->fps             = av_d2q(av_int2float(avio_rb32(pb)), INT_MAX);
78    |  if (thp->fps.den <= 0 || thp->fps.num < 0)
    1Assuming field 'den' is > 0→
    2←Assuming field 'num' is >= 0→
    3←Taking false branch→
79    |  return AVERROR_INVALIDDATA;
80    |  thp->framecnt        = avio_rb32(pb);
81    |     thp->first_framesz   = avio_rb32(pb);
82    |     pb->maxsize          = avio_rb32(pb);
83    |  if(fsize>0 && (!pb->maxsize || fsize < pb->maxsize))
    4←Assuming 'fsize' is <= 0→
84    |         pb->maxsize= fsize;
85    |  
86    |  thp->compoff         = avio_rb32(pb);
87    |                            avio_rb32(pb); /* offsetDataOffset.  */
88    |     thp->first_frame     = avio_rb32(pb);
89    |     thp->last_frame      = avio_rb32(pb);
90    |  
91    |     thp->next_framesz    = thp->first_framesz;
92    |     thp->next_frame      = thp->first_frame;
93    |  
94    |  /* Read the component structure.  */
95    |     avio_seek (pb, thp->compoff, SEEK_SET);
96    |     thp->compcount       = avio_rb32(pb);
97    |  
98    |  if (thp->compcount > FF_ARRAY_ELEMS(thp->components))
    5←Assuming the condition is false→
    6←Taking false branch→
99    |  return AVERROR_INVALIDDATA;
100   |  
101   |  /* Read the list of component types.  */
102   |  avio_read(pb, thp->components, 16);
103   |  
104   |  for (i = 0; i < thp->compcount; i++) {
    7←Assuming 'i' is < field 'compcount'→
    8←Loop condition is true.  Entering loop body→
105   |  if (thp->components[i] == 0) {
    9←Assuming the condition is false→
    10←Taking false branch→
106   |  if (thp->vst)
107   |  break;
108   |  
109   |  /* Video component.  */
110   |             st = avformat_new_stream(s, NULL);
111   |  if (!st)
112   |  return AVERROR(ENOMEM);
113   |  
114   |  /* The denominator and numerator are switched because 1/fps
115   |  is required.  */
116   |             avpriv_set_pts_info(st, 64, thp->fps.den, thp->fps.num);
117   |             st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
118   |             st->codecpar->codec_id = AV_CODEC_ID_THP;
119   |             st->codecpar->codec_tag = 0;  /* no fourcc */
120   |             st->codecpar->width = avio_rb32(pb);
121   |             st->codecpar->height = avio_rb32(pb);
122   |             st->codecpar->sample_rate = av_q2d(thp->fps);
123   |             st->nb_frames =
124   |             st->duration = thp->framecnt;
125   |             thp->vst = st;
126   |             thp->video_stream_index = st->index;
127   |  
128   |  if (thp->version == 0x11000)
129   |                 avio_rb32(pb); /* Unknown.  */
130   |         } else if (thp->components[i] == 1) {
    11←buffer read by avio_read may be partially uninitialized
131   |  if (thp->has_audio != 0)
132   |  break;
133   |  
134   |  /* Audio component.  */
135   |             st = avformat_new_stream(s, NULL);
136   |  if (!st)
137   |  return AVERROR(ENOMEM);
138   |  
139   |             st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
140   |             st->codecpar->codec_id = AV_CODEC_ID_ADPCM_THP;
141   |             st->codecpar->codec_tag = 0;  /* no fourcc */
142   |             st->codecpar->channels    = avio_rb32(pb); /* numChannels.  */
143   |             st->codecpar->sample_rate = avio_rb32(pb); /* Frequency.  */
144   |             st->duration           = avio_rb32(pb);
145   |  
146   |             avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);
147   |  
148   |             thp->audio_stream_index = st->index;
149   |             thp->has_audio = 1;
150   |         }
151   |     }
152   |  
153   |  if (!thp->vst)
154   |  return AVERROR_INVALIDDATA;
155   |  
156   |  return 0;
157   | }
158   |  
159   | static int thp_read_packet(AVFormatContext *s,
160   |                             AVPacket *pkt)