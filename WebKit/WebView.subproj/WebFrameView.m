/*	IFWebView.mm
	Copyright 2001, Apple, Inc. All rights reserved.
*/
#import <WebKit/IFWebView.h>
#import <WebKit/IFWebViewPrivate.h>
#import <WebKit/IFWebFrame.h>
#import <WebKit/IFWebDataSource.h>
#import <WebKit/IFWebDataSourcePrivate.h>
#import <WebKit/IFWebController.h>
#import <WebKit/IFBaseWebController.h>
#import <WebKit/IFDynamicScrollBarsView.h>
#import <WebKit/IFException.h>
#import <WebKit/IFWebCoreViewFactory.h>
#import <WebKit/IFTextRendererFactory.h>
#import <WebKit/WebKitDebug.h>

#import <WebFoundation/IFNSStringExtensions.h>

// Needed for the mouse move notification.
#import <AppKit/NSResponder_Private.h>

// KDE related includes
#import <khtmlview.h>
#import <qwidget.h>
#import <qpainter.h>
#import <qevent.h>
#import <html/html_documentimpl.h>


@implementation IFWebView

- initWithFrame: (NSRect) frame
{
    [super initWithFrame: frame];

    [IFWebCoreViewFactory createSharedFactory];
    [IFTextRendererFactory createSharedFactory];
    
    _private = [[IFWebViewPrivate alloc] init];

    _private->isFlipped = YES;
    _private->needsLayout = YES;

    _private->canDragTo = YES;
    _private->canDragFrom = YES;
    _private->draggingTypes = [[NSArray arrayWithObjects:@"NSFilenamesPboardType", @"NSURLPboardType", @"NSStringPboardType", nil] retain];
    [self registerForDraggedTypes:_private->draggingTypes];

    [[NSNotificationCenter defaultCenter] addObserver: self selector: @selector(windowResized:) name: NSWindowDidResizeNotification object: nil];
    [[NSNotificationCenter defaultCenter] addObserver: self selector: @selector(mouseMovedNotification:) name: NSMouseMovedNotification object: nil];
    
    return self;
}


- (void)dealloc 
{
    [self _stopPlugins];
    [[NSNotificationCenter defaultCenter] removeObserver: self];
    [_private release];
    [super dealloc];
}


- (BOOL)acceptsFirstResponder
{
    return YES;
}


- (BOOL)acceptsFirstMouse:(NSEvent *)theEvent
{
    return YES;
}


// Note that the controller is not retained.
- (id <IFWebController>)controller
{
    return _private->controller;
}


- (void)removeFromSuperview
{
    [self _stopPlugins];
    [super removeFromSuperview];
}


- (void)removeFromSuperviewWithoutNeedingDisplay
{
    [self _stopPlugins];
    [super removeFromSuperviewWithoutNeedingDisplay];
}


// This method is typically called by the view's controller when
// the data source is changed.
- (void)provisionalDataSourceChanged: (IFWebDataSource *)dataSource 
{
    NSRect r = [self frame];
    IFWebView *provisionalView;
    
    // Nasty!  Setup the cross references between the KHTMLView and
    // the KHTMLPart.
    KHTMLPart *part = [dataSource _part];

    _private->provisionalWidget = new KHTMLView (part, 0);
    part->setView (_private->provisionalWidget);

    // Create a temporary provisional view.  It will be replaced with
    // the actual view once the datasource has been committed.
    provisionalView = [[IFWebView alloc] initWithFrame: NSMakeRect (0,0,0,0)];
    _private->provisionalWidget->setView (provisionalView);
    [provisionalView release];

    _private->provisionalWidget->resize ((int)r.size.width, (int)r.size.height);
}

- (void)dataSourceChanged: (IFWebDataSource *)dataSource 
{
    IFWebViewPrivate *data = _private;

    // Setup the real view.
    if ([self _frameScrollView])
        data->provisionalWidget->setView ([self _frameScrollView]);
    else
        data->provisionalWidget->setView (self);

    // Only delete the widget if we're the top level widget.  In other
    // cases the widget is associated with a RenderFrame which will
    // delete it's widget.
    if ([dataSource isMainDocument] && data->widget)
        delete data->widget;

    data->widget = data->provisionalWidget;
    data->provisionalWidget = 0;
}

- (void)reapplyStyles
{
    KHTMLView *widget = _private->widget;

    if (widget->part()->xmlDocImpl() && 
        widget->part()->xmlDocImpl()->renderer()){
        if (_private->needsToApplyStyles){
#ifdef _KWQ_TIMING        
    double start = CFAbsoluteTimeGetCurrent();
#endif
            widget->part()->xmlDocImpl()->updateStyleSelector();
            _private->needsToApplyStyles = NO;
#ifdef _KWQ_TIMING        
    double thisTime = CFAbsoluteTimeGetCurrent() - start;
    WEBKITDEBUGLEVEL (WEBKIT_LOG_TIMING, "%s apply style seconds = %f\n", widget->part()->baseURL().url().latin1(), thisTime);
#endif
        }
    }

}


// This method should not be public until we have more completely
// understood how IFWebView will be subclassed.
- (void)layout
{
    KHTMLView *widget = _private->widget;


    // Ensure that we will receive mouse move events.  Is this the best place to put this?
    [[self window] setAcceptsMouseMovedEvents: YES];
    [[self window] _setShouldPostEventNotifications: YES];

    if (widget->part()->xmlDocImpl() && 
        widget->part()->xmlDocImpl()->renderer()){
        if (_private->needsLayout){
#ifdef _KWQ_TIMING        
    double start = CFAbsoluteTimeGetCurrent();
#endif

            WEBKITDEBUGLEVEL (WEBKIT_LOG_VIEW, "doing layout\n");
            //double start = CFAbsoluteTimeGetCurrent();
            widget->layout();
            //WebKitDebugAtLevel (WEBKIT_LOG_TIMING, "layout time %e\n", CFAbsoluteTimeGetCurrent() - start);
            _private->needsLayout = NO;
#ifdef _KWQ_TIMING        
    double thisTime = CFAbsoluteTimeGetCurrent() - start;
    WEBKITDEBUGLEVEL (WEBKIT_LOG_TIMING, "%s layout seconds = %f\n", widget->part()->baseURL().url().latin1(), thisTime);
#endif
        }
    }

}


// Stop animating animated GIFs, etc.
- (void)stopAnimations
{
    [NSException raise:IFMethodNotYetImplemented format:@"IFWebView::stopAnimations is not implemented"];
}


// Drag and drop links and images.  Others?
- (void)setCanDragFrom: (BOOL)flag
{
    _private->canDragFrom = flag;
}

- (BOOL)canDragFrom
{
    return _private->canDragFrom;
}

- (void)setCanDragTo: (BOOL)flag
{
    _private->canDragTo = flag;
}

- (BOOL)canDragTo
{
    return _private->canDragTo;
}

- (NSDragOperation)draggingEntered:(id <NSDraggingInfo>)sender
{
    NSString *dragType, *file, *URLString;
    NSArray *files;
    
    if(![self canDragTo])
        return NSDragOperationNone;
        
    dragType = [[sender draggingPasteboard] availableTypeFromArray:_private->draggingTypes];
    if([dragType isEqualToString:@"NSFilenamesPboardType"]){
        files = [[sender draggingPasteboard] propertyListForType:@"NSFilenamesPboardType"];
        
        // FIXME: We only look at the first dragged file (2931225)
        file = [files objectAtIndex:0];
        
        // FIXME: Need the file type database to know what files we handle (2927855)
        if([[file pathExtension] isEqualToString:@"html"] || [[file pathExtension] isEqualToString:@"htm"])
            return NSDragOperationCopy;
    }else if([dragType isEqualToString:@"NSURLPboardType"]){
        return NSDragOperationCopy;
    }else if([dragType isEqualToString:@"NSStringPboardType"]){
        URLString = [[sender draggingPasteboard] stringForType:@"NSStringPboardType"];
        if([URLString _IF_looksLikeAbsoluteURL])
            return NSDragOperationCopy;
    }
    return NSDragOperationNone;
}

- (BOOL)prepareForDragOperation:(id <NSDraggingInfo>)sender
{
    return YES;
}

- (BOOL)performDragOperation:(id <NSDraggingInfo>)sender
{
    IFWebDataSource *dataSource;
    IFWebFrame *frame;
    NSArray *files;
    NSString *file, *dragType;
    NSURL *URL=nil;
    
    dragType = [[sender draggingPasteboard] availableTypeFromArray:_private->draggingTypes];
    if([dragType isEqualToString:@"NSFilenamesPboardType"]){
        files = [[sender draggingPasteboard] propertyListForType:@"NSFilenamesPboardType"];
        file = [files objectAtIndex:0];
        URL = [NSURL fileURLWithPath:file];
    }else if([dragType isEqualToString:@"NSURLPboardType"]){
        // FIXME: Is this the right way to get the URL? How to test?
        URL = [NSURL URLWithString:[[sender draggingPasteboard] stringForType:@"NSURLPboardType"]];
    }else if([dragType isEqualToString:@"NSStringPboardType"]){
        URL = [NSURL URLWithString:[[sender draggingPasteboard] stringForType:@"NSStringPboardType"]];
    }
    
    if(!URL)
        return NO;
        
    dataSource = [[[IFWebDataSource alloc] initWithURL:URL] autorelease];
    frame = [[self controller] frameForView:self];
    [frame setProvisionalDataSource:dataSource];
    [frame startLoading];
    
    return YES;
}

// Returns an array of built-in context menu items for this node.
// Generally called by IFContextMenuHandlers from contextMenuItemsForNode:
#ifdef TENTATIVE_API
- (NSArray *)defaultContextMenuItemsForNode: (IFDOMNode *)
{
    [NSException raise:IFMethodNotYetImplemented format:@"IFWebView::defaultContextMenuItemsForNode: is not implemented"];
    return nil;
}
#endif

- (void)setContextMenusEnabled: (BOOL)flag
{
    [NSException raise:IFMethodNotYetImplemented format:@"IFWebView::setContextMenusEnabled: is not implemented"];
}


- (BOOL)contextMenusEnabled;
{
    return NO;
}


// Remove the selection.
- (void)deselectText
{
    [NSException raise:IFMethodNotYetImplemented format:@"IFWebView::deselectText: is not implemented"];
}



// Search from the end of the currently selected location, or from the beginning of the document if nothing
// is selected.
- (void)searchFor: (NSString *)string direction: (BOOL)forward caseSensitive: (BOOL)caseFlag
{
    [NSException raise:IFMethodNotYetImplemented format:@"IFWebView::searchFor:direction:caseSensitive: is not implemented"];
}


// Get an attributed string that represents the current selection.
- (NSAttributedString *)selectedText
{
    [NSException raise:IFMethodNotYetImplemented format:@"IFWebView::selectedText is not implemented"];
    return nil;
}


- (BOOL)isOpaque
{
    return YES;
}


#ifdef DELAY_LAYOUT
- delayLayout: sender
{
    [NSObject cancelPreviousPerformRequestsWithTarget: self selector: @selector(delayLayout:) object: self];
    WEBKITDEBUG("KWQHTMLView:  delayLayout called\n");
    [self setNeedsLayout: YES];
    [self setNeedsDisplay: YES];
}

-(void)notificationReceived:(NSNotification *)notification
{
    if ([[notification name] rangeOfString: @"uri-fin-"].location == 0){
        WEBKITDEBUG1("KWQHTMLView: Received notification, %s\n", DEBUG_OBJECT([notification name]));
        [self performSelector:@selector(delayLayout:) withObject:self afterDelay:(NSTimeInterval)0.5];
    }
}
#else
-(void)notificationReceived:(NSNotification *)notification
{
    if ([[notification name] rangeOfString: @"uri-fin-"].location == 0){
        [self setNeedsLayout: YES];
        [self setNeedsDisplay: YES];
    }
}
#endif

- (void)setNeedsDisplay:(BOOL)flag
{
    WEBKITDEBUGLEVEL (WEBKIT_LOG_VIEW, "flag = %d\n", (int)flag);
    [super setNeedsDisplay: flag];
}


- (void)setNeedsLayout: (bool)flag
{
    WEBKITDEBUGLEVEL (WEBKIT_LOG_VIEW, "flag = %d\n", (int)flag);
    _private->needsLayout = flag;
}


- (void)setNeedsToApplyStyles: (bool)flag
{
    WEBKITDEBUGLEVEL (WEBKIT_LOG_VIEW, "flag = %d\n", (int)flag);
    _private->needsToApplyStyles = flag;
}


// This should eventually be removed.
- (void)drawRect:(NSRect)rect {
    KHTMLView *widget = _private->widget;
    //IFWebViewPrivate *data = _private;

    //if (data->provisionalWidget != 0){
    //    WEBKITDEBUGLEVEL (WEBKIT_LOG_VIEW, "not drawing, frame in provisional state.\n");
    //    return;
    //}
    
    // Draw plain white bg in empty case, to avoid redraw weirdness when
    // no page is yet loaded (2890818). We may need to modify this to always
    // draw the background color, in which case we'll have to make sure the
    // no-widget case is still handled correctly.
    if (widget == 0l) {
        [[NSColor whiteColor] set];
        NSRectFill(rect);
        return;
    }

    WEBKITDEBUGLEVEL (WEBKIT_LOG_VIEW, "drawing\n");

    [self reapplyStyles];

    [self layout];

#ifdef _KWQ_TIMING
    double start = CFAbsoluteTimeGetCurrent();
#endif
    QPainter p(widget);

    [self lockFocus];

    //double start = CFAbsoluteTimeGetCurrent();
    widget->drawContents( &p, (int)rect.origin.x,
                              (int)rect.origin.y,
                              (int)rect.size.width,
                               (int)rect.size.height );
    //WebKitDebugAtLevel (WEBKIT_LOG_TIMING, "draw time %e\n", CFAbsoluteTimeGetCurrent() - start);

#ifdef DEBUG_LAYOUT
    NSRect vframe = [self frame];
    [[NSColor blackColor] set];
    NSBezierPath *path;
    path = [NSBezierPath bezierPath];
    [path setLineWidth:(float)0.1];
    [path moveToPoint:NSMakePoint(0, 0)];
    [path lineToPoint:NSMakePoint(vframe.size.width, vframe.size.height)];
    [path closePath];
    [path stroke];
    path = [NSBezierPath bezierPath];
    [path setLineWidth:(float)0.1];
    [path moveToPoint:NSMakePoint(0, vframe.size.height)];
    [path lineToPoint:NSMakePoint(vframe.size.width, 0)];
    [path closePath];
    [path stroke];
#endif

    [self unlockFocus];

#ifdef _KWQ_TIMING
    double thisTime = CFAbsoluteTimeGetCurrent() - start;
    WEBKITDEBUGLEVEL (WEBKIT_LOG_TIMING, "%s draw seconds = %f\n", widget->part()->baseURL().url().latin1(), thisTime);
#endif
}

- (void)setIsFlipped: (bool)flag
{
    _private->isFlipped = flag;
}


- (BOOL)isFlipped 
{
    return _private->isFlipped;
}


- (void)windowResized: (NSNotification *)notification
{
    if ([notification object] == [self window])
        [self setNeedsLayout: YES];
}

- (void)_addModifiers:(unsigned)modifiers toState:(int *)state
{
    if (modifiers & NSControlKeyMask)
        *state |= Qt::ControlButton;
    if (modifiers & NSShiftKeyMask)
        *state |= Qt::ShiftButton;
    if (modifiers & NSAlternateKeyMask)
        *state |= Qt::AltButton;
    // Mapping command to meta is slightly questionable
    if (modifiers & NSCommandKeyMask)
        *state |= Qt::MetaButton;
}

- (void)mouseUp: (NSEvent *)event
{
    int button, state;
     
    if ([event type] == NSLeftMouseUp){
        button = Qt::LeftButton;
        state = Qt::LeftButton;
    }
    else if ([event type] == NSRightMouseUp){
        button = Qt::RightButton;
        state = Qt::RightButton;
    }
    else if ([event type] == NSOtherMouseUp){
        button = Qt::MidButton;
        state = Qt::MidButton;
    }
    else {
        [NSException raise:IFRuntimeError format:@"IFWebView::mouseUp: unknown button type"];
        button = 0; state = 0; // Shutup the compiler.
    }
    NSPoint p = [event locationInWindow];
    
    [self _addModifiers:[event modifierFlags] toState:&state];

    QMouseEvent kEvent(QEvent::MouseButtonPress, QPoint((int)p.x, (int)p.y), button, state);
    KHTMLView *widget = _private->widget;
    if (widget != 0l) {
        widget->viewportMouseReleaseEvent(&kEvent);
    }
}

- (void)mouseDown: (NSEvent *)event
{
    int button, state;
     
    if ([event type] == NSLeftMouseDown){
        button = Qt::LeftButton;
        state = Qt::LeftButton;
    }
    else if ([event type] == NSRightMouseDown){
        button = Qt::RightButton;
        state = Qt::RightButton;
    }
    else if ([event type] == NSOtherMouseDown){
        button = Qt::MidButton;
        state = Qt::MidButton;
    }
    else {
        [NSException raise:IFRuntimeError format:@"IFWebView::mouseDown: unknown button type"];
        button = 0; state = 0; // Shutup the compiler.
    }
    NSPoint p = [event locationInWindow];
    
    [self _addModifiers:[event modifierFlags] toState:&state];

    QMouseEvent kEvent(QEvent::MouseButtonPress, QPoint((int)p.x, (int)p.y), button, state);
    KHTMLView *widget = _private->widget;
    if (widget != 0l) {
        widget->viewportMousePressEvent(&kEvent);
    }
}

- (void)mouseMovedNotification: (NSNotification *)notification
{
    NSEvent *event = [(NSDictionary *)[notification userInfo] objectForKey: @"NSEvent"];
    NSPoint p = [event locationInWindow];
    
    QMouseEvent kEvent(QEvent::MouseButtonPress, QPoint((int)p.x, (int)p.y), 0, 0);
    KHTMLView *widget = _private->widget;
    if (widget != 0l) {
        widget->viewportMouseMoveEvent(&kEvent);
    }
}

- (void)mouseDragged: (NSEvent *)event
{
    NSPoint p = [event locationInWindow];
    //WebKitDebugAtLevel (WEBKIT_LOG_EVENTS, "mouseDragged %f, %f\n", p.x, p.y);
}

- (void)keyDown: (NSEvent *)event
{
    NSLog (@"keyDown: %@\n", event);
    int state = 0;
    
    [self _addModifiers:[event modifierFlags] toState:&state];
    QKeyEvent kEvent(QEvent::KeyPress, 0, 0, state, NSSTRING_TO_QSTRING([event characters]), [event isARepeat], 1);

    
    KHTMLView *widget = _private->widget;
    if (widget != 0l)
        widget->keyPressEvent(&kEvent);
}


- (void)keyUp: (NSEvent *)event
{
    NSLog (@"keyUp: %@\n", event);
    int state = 0;
    
    [self _addModifiers:[event modifierFlags] toState:&state];
    QKeyEvent kEvent(QEvent::KeyPress, 0, 0, state, NSSTRING_TO_QSTRING([event characters]), [event isARepeat], 1);

    
    KHTMLView *widget = _private->widget;
    if (widget != 0l)
        widget->keyReleaseEvent(&kEvent);
}


@end
