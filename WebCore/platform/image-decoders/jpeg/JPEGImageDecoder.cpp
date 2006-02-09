/*
 * Copyright (C) 2006 Apple Computer, Inc.
 *
 * Portions are Copyright (C) 2001 mozilla.org
 *
 * Other contributors:
 *   Stuart Parmenter <pavlov@mozilla.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Alternatively, the contents of this file may be used under the terms
 * of either the Mozilla Public License Version 1.1, found at
 * http://www.mozilla.org/MPL/ (the "MPL") or the GNU General Public
 * License Version 2.0, found at http://www.fsf.org/copyleft/gpl.html
 * (the "GPL"), in which case the provisions of the MPL or the GPL are
 * applicable instead of those above.  If you wish to allow use of your
 * version of this file only under the terms of one of those two
 * licenses (the MPL or the GPL) and not to allow others to use your
 * version of this file under the LGPL, indicate your decision by
 * deletingthe provisions above and replace them with the notice and
 * other provisions required by the MPL or the GPL, as the case may be.
 * If you do not delete the provisions above, a recipient may use your
 * version of this file under any of the LGPL, the MPL or the GPL.
 */

#include "JPEGImageDecoder.h"

namespace WebCore {

class JPEGImageReader
{
public:
    JPEGImageReader(JPEGImageDecoder* decoder)
    : m_readOffset(0)
    {
    }

    ~JPEGImageReader()
    {
        close();
    }

    void close() {
        m_readOffset = 0;
    }

    void decode(const ByteArray& data, bool sizeOnly)
    {
        m_decodingSizeOnly = sizeOnly;

        // FIXME: Implement
    }

private:
    unsigned m_readOffset;
    bool m_decodingSizeOnly;
};

JPEGImageDecoder::JPEGImageDecoder()
: m_reader(0)
{}

JPEGImageDecoder::~JPEGImageDecoder()
{
    delete m_reader;
}

// Take the data and store it.
void JPEGImageDecoder::setData(const ByteArray& data, bool allDataReceived)
{
    if (m_failed)
        return;

    // Cache our new data.
    ImageDecoder::setData(data, allDataReceived);

    // Create the JPEG reader.
    if (!m_reader && !m_failed)
        m_reader = new JPEGImageReader(this);
}

// Whether or not the size information has been decoded yet.
bool JPEGImageDecoder::isSizeAvailable() const
{
    // If we have pending data to decode, send it to the JPEG reader now.
    if (!m_sizeAvailable && m_reader) {
        if (m_failed)
            return false;

        // The decoder will go ahead and aggressively consume everything up until the
        // size is encountered.
        decode(true);
    }

    return m_sizeAvailable;
}

RGBA32Buffer JPEGImageDecoder::frameBufferAtIndex(size_t index)
{
    if (index != 0)
        return RGBA32Buffer();

    if (m_frameBufferCache.isEmpty())
        m_frameBufferCache.resize(1);

    const RGBA32Buffer& frame = m_frameBufferCache[0];
    if (frame.status() != RGBA32Buffer::FrameComplete && m_reader)
        // Decode this frame.
        decode();

    return frame;
}

// Feed data to the JPEG reader.
void JPEGImageDecoder::decode(bool sizeOnly) const
{
    if (m_failed)
        return;

    m_reader->decode(m_data, sizeOnly);
    
    if (m_failed || (!m_frameBufferCache.isEmpty() && m_frameBufferCache[0].status() == RGBA32Buffer::FrameComplete)) {
        delete m_reader;
        m_reader = 0;
    }
}

}
