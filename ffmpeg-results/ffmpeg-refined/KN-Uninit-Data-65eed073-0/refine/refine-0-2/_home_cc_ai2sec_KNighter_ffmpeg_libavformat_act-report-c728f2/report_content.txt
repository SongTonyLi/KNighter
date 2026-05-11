### Report Summary

File:| /home/cc/ai2sec/KNighter/ffmpeg/libavformat/act.c  
---|---  
Warning:| line 140, column 22  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


60    |  return AVPROBE_SCORE_MAX;
61    | }
62    |  
63    | static int read_header(AVFormatContext *s)
64    | {
65    |     ACTContext* ctx = s->priv_data;
66    |     AVIOContext *pb = s->pb;
67    |  int size;
68    |     AVStream* st;
69    |  
70    |  int min,sec,msec;
71    |  
72    |     st = avformat_new_stream(s, NULL);
73    |  if (!st)
74    |  return AVERROR(ENOMEM);
75    |  
76    |     avio_skip(pb, 16);
77    |     size=avio_rl32(pb);
78    |     ff_get_wav_header(s, pb, st->codecpar, size, 0);
79    |  
80    |  /*
81    |  8000Hz (Fine-rec) file format has 10 bytes long
82    |  packets with 10ms of sound data in them
83    |  */
84    |  if (st->codecpar->sample_rate != 8000) {
85    |         av_log(s, AV_LOG_ERROR, "Sample rate %d is not supported.\n", st->codecpar->sample_rate);
86    |  return AVERROR_INVALIDDATA;
87    |     }
88    |  
89    |     st->codecpar->frame_size=80;
90    |     st->codecpar->channels=1;
91    |     avpriv_set_pts_info(st, 64, 1, 100);
92    |  
93    |     st->codecpar->codec_id=AV_CODEC_ID_G729;
94    |  
95    |     avio_seek(pb, 257, SEEK_SET);
96    |     msec=avio_rl16(pb);
97    |     sec=avio_r8(pb);
98    |     min=avio_rl32(pb);
99    |  
100   |     st->duration = av_rescale(1000*(min*60+sec)+msec, st->codecpar->sample_rate, 1000 * st->codecpar->frame_size);
101   |  
102   |     ctx->bytes_left_in_chunk=CHUNK_SIZE;
103   |  
104   |     avio_seek(pb, 512, SEEK_SET);
105   |  
106   |  return 0;
107   | }
108   |  
109   |  
110   | static int read_packet(AVFormatContext *s,
111   |                           AVPacket *pkt)
112   | {
113   |  ACTContext *ctx = s->priv_data;
114   |     AVIOContext *pb = s->pb;
115   |  int ret;
116   |  int frame_size=s->streams[0]->codecpar->sample_rate==8000?10:22;
    1Assuming field 'sample_rate' is not equal to 8000→
    2←'?' condition is false→
117   |  
118   |  
119   |  if(s->streams[0]->codecpar->sample_rate2.1Field 'sample_rate' is not equal to 8000==8000)
    3←Taking false branch→
120   |         ret=av_new_packet(pkt, 10);
121   |  else
122   |  ret=av_new_packet(pkt, 11);
123   |  
124   |  if(ret)
    4←Assuming 'ret' is 0→
125   |  return ret;
126   |  
127   |  if(s->streams[0]->codecpar->sample_rate==4400 && !ctx->second_packet)
    5←Assuming field 'sample_rate' is equal to 4400→
    6←Assuming field 'second_packet' is 0→
    7←Taking true branch→
128   |     {
129   |  ret = avio_read(pb, ctx->audio_buffer, frame_size);
130   |  
131   |  if(ret<0)
    8←Assuming 'ret' is >= 0→
    9←Taking false branch→
132   |  return ret;
133   |  if(ret!=frame_size)
    10←Assuming 'ret' is equal to 'frame_size'→
    11←Taking false branch→
134   |  return AVERROR(EIO);
135   |  
136   |  pkt->data[0]=ctx->audio_buffer[11];
137   |         pkt->data[1]=ctx->audio_buffer[0];
138   |         pkt->data[2]=ctx->audio_buffer[12];
139   |         pkt->data[3]=ctx->audio_buffer[1];
140   |  pkt->data[4]=ctx->audio_buffer[13];
    12←buffer read by avio_read may be partially uninitialized
141   |         pkt->data[5]=ctx->audio_buffer[2];
142   |         pkt->data[6]=ctx->audio_buffer[14];
143   |         pkt->data[7]=ctx->audio_buffer[3];
144   |         pkt->data[8]=ctx->audio_buffer[15];
145   |         pkt->data[9]=ctx->audio_buffer[4];
146   |         pkt->data[10]=ctx->audio_buffer[16];
147   |  
148   |         ctx->second_packet=1;
149   |     }
150   |  else if(s->streams[0]->codecpar->sample_rate==4400 && ctx->second_packet)
151   |     {
152   |         pkt->data[0]=ctx->audio_buffer[5];
153   |         pkt->data[1]=ctx->audio_buffer[17];
154   |         pkt->data[2]=ctx->audio_buffer[6];
155   |         pkt->data[3]=ctx->audio_buffer[18];
156   |         pkt->data[4]=ctx->audio_buffer[7];
157   |         pkt->data[5]=ctx->audio_buffer[19];
158   |         pkt->data[6]=ctx->audio_buffer[8];
159   |         pkt->data[7]=ctx->audio_buffer[20];
160   |         pkt->data[8]=ctx->audio_buffer[9];
161   |         pkt->data[9]=ctx->audio_buffer[21];
162   |         pkt->data[10]=ctx->audio_buffer[10];
163   |  
164   |         ctx->second_packet=0;
165   |     }
166   |  else // 8000 Hz
167   |     {
168   |         ret = avio_read(pb, ctx->audio_buffer, frame_size);
169   |  
170   |  if(ret<0)