/*
 * Copyright (C) 2009 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "MediaExtractor"
#include <utils/Log.h>

#include "include/AMRExtractor.h"
#include "include/MP3Extractor.h"
#include "include/MPEG4Extractor.h"
#include "include/WAVExtractor.h"
#include "include/OggExtractor.h"
#include "include/MPEG2PSExtractor.h"
#include "include/MPEG2TSExtractor.h"
#include "include/DRMExtractor.h"
#include "include/WVMExtractor.h"
#include "include/FLACExtractor.h"
#include "include/AACExtractor.h"
#include "include/MidiExtractor.h"

#include "matroska/MatroskaExtractor.h"

#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaExtractor.h>
#include <media/stagefright/MetaData.h>
#include <utils/String8.h>

namespace android {

MediaExtractor::Plugin MediaExtractor::sPlugin;

sp<MetaData> MediaExtractor::getMetaData() {
    return new MetaData;
}

uint32_t MediaExtractor::flags() const {
    return CAN_SEEK_BACKWARD | CAN_SEEK_FORWARD | CAN_PAUSE | CAN_SEEK;
}

// static
sp<MediaExtractor> MediaExtractor::Create(
        const sp<DataSource> &source, const char *mime,
        const uint32_t flags) {
    sp<AMessage> meta;

    bool secondPass = false;

    String8 tmp;
retry:
    if (secondPass || mime == NULL) {
        float confidence;
        if (secondPass) {
            confidence = 3.14f;
        }
        if (!source->sniff(&tmp, &confidence, &meta)) {
            ALOGV("FAILED to autodetect media content.");

            return NULL;
        }

        mime = tmp.string();
        ALOGV("Autodetected media content as '%s' with confidence %.2f",
             mime, confidence);
    }

    bool isDrm = false;
    // DRM MIME type syntax is "drm+type+original" where
    // type is "es_based" or "container_based" and
    // original is the content's cleartext MIME type
    if (!strncmp(mime, "drm+", 4)) {
        const char *originalMime = strchr(mime+4, '+');
        if (originalMime == NULL) {
            // second + not found
            return NULL;
        }
        ++originalMime;
        if (!strncmp(mime, "drm+es_based+", 13)) {
            // DRMExtractor sets container metadata kKeyIsDRM to 1
            return new DRMExtractor(source, originalMime);
        } else if (!strncmp(mime, "drm+container_based+", 20)) {
            mime = originalMime;
            isDrm = true;
        } else {
            return NULL;
        }
    }
    sp<MediaExtractor> ret = NULL;
    AString extractorName;
    if ((ret = AVFactory::get()->createExtendedExtractor(source, mime, meta)) != NULL) {
    } else if (meta.get() && meta->findString("extended-extractor-use", &extractorName)
            && sPlugin.create) {
        ALOGI("Use extended extractor for the special mime(%s) or codec", mime);
        ret = sPlugin.create(source, mime, meta);
    } else if (!strcasecmp(mime, MEDIA_MIMETYPE_CONTAINER_MPEG4)
            || !strcasecmp(mime, "audio/mp4")) {
        ret = new MPEG4Extractor(source);
    } else if (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_MPEG)) {
        ret = new MP3Extractor(source, meta);
    } else if (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_AMR_NB)
            || !strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_AMR_WB)) {
        ret = new AMRExtractor(source);
    } else if (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_FLAC)) {
        ret = new FLACExtractor(source);
    } else if (!strcasecmp(mime, MEDIA_MIMETYPE_CONTAINER_WAV)) {
        ret = new WAVExtractor(source);
    } else if (!strcasecmp(mime, MEDIA_MIMETYPE_CONTAINER_OGG)) {
        ret = new OggExtractor(source);
    } else if (!strcasecmp(mime, MEDIA_MIMETYPE_CONTAINER_MATROSKA)) {
        ret = new MatroskaExtractor(source);
    } else if (!strcasecmp(mime, MEDIA_MIMETYPE_CONTAINER_MPEG2TS)) {
        ret = new MPEG2TSExtractor(source);
    } else if (!strcasecmp(mime, MEDIA_MIMETYPE_CONTAINER_WVM)) {
        // Return now.  WVExtractor should not have the DrmFlag set in the block below.
        return new WVMExtractor(source);
    } else if (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_AAC_ADTS)) {
        ret = new AACExtractor(source, meta);
    } else if (!strcasecmp(mime, MEDIA_MIMETYPE_CONTAINER_MPEG2PS)) {
        ret = new MPEG2PSExtractor(source);
    } else if (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_MIDI)) {
        ret = new MidiExtractor(source);
    } else if (!isDrm && sPlugin.create) {
        ret = sPlugin.create(source, mime, meta);
    }

    ret = AVFactory::get()->updateExtractor(ret, source, mime, meta, flags);
    if (ret != NULL) {
       if (isDrm) {
           ret->setDrmFlag(true);
       } else {
           ret->setDrmFlag(false);
       }
    }

    if (ret != NULL) {

        if (!secondPass && ( ret->countTracks() == 0 ||
                    (!strncasecmp("video/", mime, 6) && ret->countTracks() < 2) ) ) {
            secondPass = true;
            goto retry;
        }
    }

    return ret;
}

}  // namespace android
