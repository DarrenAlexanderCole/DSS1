#include "Haptics.h"

#import <AppKit/AppKit.h>

namespace dss1::ui::haptics
{

void tap()
{
    // The same feel as snapping to a guide in a Mac app.
    [[NSHapticFeedbackManager defaultPerformer] performFeedbackPattern: NSHapticFeedbackPatternAlignment
                                                       performanceTime: NSHapticFeedbackPerformanceTimeNow];
}

} // namespace dss1::ui::haptics
