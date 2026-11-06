// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "MicrophoneAccess.hpp"

#import <AVFoundation/AVFoundation.h>

namespace magda {

MicrophoneAccess systemMicrophoneAccess() {
    // Asking without a usage description aborts the process, as in a bare test binary.
    if ([NSBundle.mainBundle objectForInfoDictionaryKey:@"NSMicrophoneUsageDescription"] == nil)
        return {};

    return {.undecided =
                [] {
                    return [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio] ==
                           AVAuthorizationStatusNotDetermined;
                },
            .request =
                [](std::function<void()> answered) {
                    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                                             completionHandler:^(BOOL) {
                                               answered();
                                             }];
                }};
}

}  // namespace magda
