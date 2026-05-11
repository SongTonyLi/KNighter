### Report Summary

File:| format/id3v2.c  
---|---  
Warning:| line 149, column 13  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


97    | };
98    |  
99    | attribute_nonstring  const char ff_id3v2_4_tags[][4] = {
100   |  "TDEN", "TDOR", "TDRC", "TDRL", "TDTG", "TIPL", "TMCL", "TMOO",
101   |  "TPRO", "TSOA", "TSOP", "TSOT", "TSST",
102   |     { 0 },
103   | };
104   |  
105   | attribute_nonstring const char ff_id3v2_3_tags[][4] = {
106   |  "TDAT", "TIME", "TORY", "TRDA", "TSIZ", "TYER",
107   |     { 0 },
108   | };
109   |  
110   | const char * const ff_id3v2_picture_types[21] = {
111   |  "Other",
112   |  "32x32 pixels 'file icon'",
113   |  "Other file icon",
114   |  "Cover (front)",
115   |  "Cover (back)",
116   |  "Leaflet page",
117   |  "Media (e.g. label side of CD)",
118   |  "Lead artist/lead performer/soloist",
119   |  "Artist/performer",
120   |  "Conductor",
121   |  "Band/Orchestra",
122   |  "Composer",
123   |  "Lyricist/text writer",
124   |  "Recording Location",
125   |  "During recording",
126   |  "During performance",
127   |  "Movie/video screen capture",
128   |  "A bright coloured fish",
129   |  "Illustration",
130   |  "Band/artist logotype",
131   |  "Publisher/Studio logotype",
132   | };
133   |  
134   | const CodecMime ff_id3v2_mime_tags[] = {
135   |     { "image/gif",  AV_CODEC_ID_GIF   },
136   |     { "image/jpeg", AV_CODEC_ID_MJPEG },
137   |     { "image/jpg",  AV_CODEC_ID_MJPEG },
138   |     { "image/png",  AV_CODEC_ID_PNG   },
139   |     { "image/tiff", AV_CODEC_ID_TIFF  },
140   |     { "image/bmp",  AV_CODEC_ID_BMP   },
141   |     { "image/webp", AV_CODEC_ID_WEBP  },
142   |     { "JPG",        AV_CODEC_ID_MJPEG }, /* ID3v2.2  */
143   |     { "PNG",        AV_CODEC_ID_PNG   }, /* ID3v2.2  */
144   |     { "",           AV_CODEC_ID_NONE  },
145   | };
146   |  
147   | int ff_id3v2_match(const uint8_t *buf, const char *magic)
148   | {
149   |  return buf[0]         == magic[0] &&
    9←buffer read by avio_read may be partially uninitialized
150   |             buf[1]         == magic[1] &&
151   |             buf[2]         == magic[2] &&
152   |             buf[3]         != 0xff     &&
153   |             buf[4]         != 0xff     &&
154   |            (buf[6] & 0x80) == 0        &&
155   |            (buf[7] & 0x80) == 0        &&
156   |            (buf[8] & 0x80) == 0        &&
157   |            (buf[9] & 0x80) == 0;
158   | }
159   |  
160   | int ff_id3v2_tag_len(const uint8_t *buf)
161   | {
162   |  int len = ((buf[6] & 0x7f) << 21) +
163   |               ((buf[7] & 0x7f) << 14) +
164   |               ((buf[8] & 0x7f) << 7) +
165   |               (buf[9] & 0x7f) +
166   |  ID3v2_HEADER_SIZE;
167   |  if (buf[5] & 0x10)
168   |         len += ID3v2_HEADER_SIZE;
169   |  return len;
170   | }
171   |  
172   | static unsigned int get_size(AVIOContext *s, int len)
173   | {
174   |  int v = 0;
175   |  while (len--)
176   |         v = (v << 7) + (avio_r8(s) & 0x7F);
177   |  return v;
178   | }
179   |  
1040  |  goto seek;
1041  |                         }
1042  |                         tlen = err;
1043  |                     }
1044  |  
1045  |                     err = uncompress(uncompressed_buffer, &dlen, buffer, tlen);
1046  |  if (err != Z_OK) {
1047  |                         av_log(s, AV_LOG_ERROR, "Failed to uncompress tag: %d\n", err);
1048  |  goto seek;
1049  |                     }
1050  |                     ffio_init_read_context(&pb_local, uncompressed_buffer, dlen);
1051  |                     tlen = dlen;
1052  |                     pbx = &pb_local.pub; // read from sync buffer
1053  |                 }
1054  | #endif
1055  |  if (tag[0] == 'T')
1056  |  /* parse text tag */
1057  |                 read_ttag(s, pbx, tlen, metadata, tag);
1058  |  else if (!memcmp(tag, "USLT", 4))
1059  |                 read_uslt(s, pbx, tlen, metadata);
1060  |  else if (!strcmp(tag, comm_frame))
1061  |                 read_comment(s, pbx, tlen, metadata);
1062  |  else
1063  |  /* parse special meta tag */
1064  |                 extra_func->read(s, pbx, tlen, tag, extra_meta, isv34);
1065  |         } else if (!tag[0]) {
1066  |  if (tag[1])
1067  |                 av_log(s, AV_LOG_WARNING, "invalid frame id, assuming padding\n");
1068  |             avio_skip(pb, tlen);
1069  |  break;
1070  |         }
1071  |  /* Skip to end of tag */
1072  | seek:
1073  |         avio_seek(pb, next, SEEK_SET);
1074  |     }
1075  |  
1076  |  /* Footer preset, always 10 bytes, skip over it */
1077  |  if (version == 4 && flags & 0x10)
1078  |         end += 10;
1079  |  
1080  | error:
1081  |  if (reason)
1082  |         av_log(s, AV_LOG_INFO, "ID3v2.%d tag skipped, cannot handle %s\n",
1083  |                version, reason);
1084  |     avio_seek(pb, end, SEEK_SET);
1085  |     av_free(buffer);
1086  |     av_free(uncompressed_buffer);
1087  |  return;
1088  | }
1089  |  
1090  | static void id3v2_read_internal(AVIOContext *pb, AVDictionary **metadata,
1091  |                                 AVFormatContext *s, const char *magic,
1092  |                                 ID3v2ExtraMeta **extra_metap, int64_t max_search_size)
1093  | {
1094  |  int len, ret;
1095  |     uint8_t buf[ID3v2_HEADER_SIZE];
1096  |     ExtraMetaList extra_meta = { NULL };
1097  |  int found_header;
1098  |     int64_t start, off;
1099  |  
1100  |  if (extra_metap)
    2←Assuming 'extra_metap' is null→
1101  |         *extra_metap = NULL;
1102  |  
1103  |  if (max_search_size && max_search_size < ID3v2_HEADER_SIZE)
    3←Assuming 'max_search_size' is 0→
1104  |  return;
1105  |  
1106  |  start = avio_tell(pb);
1107  |  do {
1108  |  /* save the current offset in case there's nothing to read/skip */
1109  |  off = avio_tell(pb);
1110  |  if (max_search_size3.1'max_search_size' is 0 && off - start >= max_search_size - ID3v2_HEADER_SIZE) {
1111  |             avio_seek(pb, off, SEEK_SET);
1112  |  break;
1113  |         }
1114  |  
1115  |  ret = ffio_ensure_seekback(pb, ID3v2_HEADER_SIZE);
1116  |  if (ret >= 0)
    4←Assuming 'ret' is >= 0→
    5←Taking true branch→
1117  |  ret = avio_read(pb, buf, ID3v2_HEADER_SIZE);
1118  |  if (ret != ID3v2_HEADER_SIZE) {
    6←Assuming 'ret' is equal to ID3v2_HEADER_SIZE→
    7←Taking false branch→
1119  |             avio_seek(pb, off, SEEK_SET);
1120  |  break;
1121  |         }
1122  |  found_header = ff_id3v2_match(buf, magic);
    8←Calling 'ff_id3v2_match'→
1123  |  if (found_header) {
1124  |  /* parse ID3v2 header */
1125  |             len = ((buf[6] & 0x7f) << 21) |
1126  |                   ((buf[7] & 0x7f) << 14) |
1127  |                   ((buf[8] & 0x7f) << 7) |
1128  |                    (buf[9] & 0x7f);
1129  |             id3v2_parse(pb, metadata, s, len, buf[3], buf[5],
1130  |                         extra_metap ? &extra_meta : NULL);
1131  |         } else {
1132  |             avio_seek(pb, off, SEEK_SET);
1133  |         }
1134  |     } while (found_header);
1135  |     ff_metadata_conv(metadata, NULL, ff_id3v2_34_metadata_conv);
1136  |     ff_metadata_conv(metadata, NULL, id3v2_2_metadata_conv);
1137  |     ff_metadata_conv(metadata, NULL, ff_id3v2_4_metadata_conv);
1138  |     merge_date(metadata);
1139  |  if (extra_metap)
1140  |         *extra_metap = extra_meta.head;
1141  | }
1142  |  
1143  | void ff_id3v2_read_dict(AVIOContext *pb, AVDictionary **metadata,
1144  |  const char *magic, ID3v2ExtraMeta **extra_meta)
1145  | {
1146  |     id3v2_read_internal(pb, metadata, NULL, magic, extra_meta, 0);
1147  | }
1148  |  
1149  | void ff_id3v2_read(AVFormatContext *s, const char *magic,
1150  |                    ID3v2ExtraMeta **extra_meta, unsigned int max_search_size)
1151  | {
1152  |  id3v2_read_internal(s->pb, &s->metadata, s, magic, extra_meta, max_search_size);
    1Calling 'id3v2_read_internal'→
1153  | }
1154  |  
1155  | void ff_id3v2_free_extra_meta(ID3v2ExtraMeta **extra_meta)
1156  | {
1157  |     ID3v2ExtraMeta *current = *extra_meta, *next;
1158  |  const ID3v2EMFunc *extra_func;
1159  |  
1160  |  while (current) {
1161  |  if ((extra_func = get_extra_meta_func(current->tag, 1)))
1162  |             extra_func->free(¤t->data);
1163  |         next = current->next;
1164  |         av_freep(¤t);
1165  |         current = next;
1166  |     }
1167  |  
1168  |     *extra_meta = NULL;
1169  | }
1170  |  
1171  | int ff_id3v2_parse_apic(AVFormatContext *s, ID3v2ExtraMeta *extra_meta)
1172  | {
1173  |     ID3v2ExtraMeta *cur;
1174  |  
1175  |  for (cur = extra_meta; cur; cur = cur->next) {
1176  |         ID3v2ExtraMetaAPIC *apic;
1177  |         AVStream *st;
1178  |  int ret;
1179  |  
1180  |  if (strcmp(cur->tag, "APIC"))
1181  |  continue;
1182  |         apic = &cur->data.apic;