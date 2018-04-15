/*
 * BRLTTY - A background process providing access to the console screen (when in
 *          text mode) for a blind person using a refreshable braille display.
 *
 * Copyright (C) 1995-2018 by The BRLTTY Developers.
 *
 * BRLTTY comes with ABSOLUTELY NO WARRANTY.
 *
 * This is free software, placed under the terms of the
 * GNU Lesser General Public License, as published by the Free Software
 * Foundation; either version 2.1 of the License, or (at your option) any
 * later version. Please see the file LICENSE-LGPL for details.
 *
 * Web Page: http://brltty.com/
 *
 * This software is maintained by Dave Mielke <dave@mielke.cc>.
 */

package org.a11y.brltty.android;

import java.util.Arrays;
import java.util.List;
import java.util.ArrayList;
import java.util.Map;
import java.util.HashMap;

import android.util.Log;

import android.view.accessibility.AccessibilityNodeInfo;

import android.graphics.Rect;

public class ScreenElementList extends ArrayList<ScreenElement> {
  private static final String LOG_TAG = ScreenElementList.class.getName();

  public ScreenElementList () {
    super();
  }

  public void log () {
    Log.d(LOG_TAG, "begin screen element list");

    for (ScreenElement element : this) {
      Log.d(LOG_TAG, "screen element: " + element.getElementText());
    }

    Log.d(LOG_TAG, "end screen element list");
  }

  private int atTopCount = 0;

  public final void addAtTop (VirtualScreenElement element) {
    add(atTopCount++, element);
  }

  public final void addAtTop (int text, int action, int key) {
    addAtTop(new VirtualScreenElement(text, action, key));
  }

  public final void addAtTop (int text, int action) {
    addAtTop(new VirtualScreenElement(text, action));
  }

  public final void addAtBottom (int text, int action, int key) {
    add(new VirtualScreenElement(text, action, key));
  }

  public final void addAtBottom (int text, int action) {
    add(new VirtualScreenElement(text, action));
  }

  private final Map<AccessibilityNodeInfo, ScreenElement> nodeToScreenElement =
        new HashMap<AccessibilityNodeInfo, ScreenElement>();

  public final void add (String text, AccessibilityNodeInfo node) {
    node = AccessibilityNodeInfo.obtain(node);
    ScreenElement element = new RealScreenElement(text, node);
    add(element);
    nodeToScreenElement.put(node, element);
  }

  public final ScreenElement get (AccessibilityNodeInfo node) {
    return nodeToScreenElement.get(node);
  }

  private final void addByContainer (NodeComparator nodeComparator, AccessibilityNodeInfo... nodes) {
    {
      final int count = nodes.length;
      if (count == 0) return;
      if (count > 1) Arrays.sort(nodes, nodeComparator);
    }

    for (AccessibilityNodeInfo node : nodes) {
      if (node == null) break;

      try {
        {
          ScreenElement element = get(node);
          if (element != null) add(element);
        }

        {
          int count = node.getChildCount();

          if (count > 0) {
            AccessibilityNodeInfo[] children = new AccessibilityNodeInfo[count];

            for (int index=0; index<count; index+=1) {
              children[index] = node.getChild(index);
            }

            addByContainer(nodeComparator, children);
          }
        }
      } finally {
        node.recycle();
        node = null;
      }
    }
  }

  private final void sortByVisualLocation () {
    if (size() < 2) return;
    AccessibilityNodeInfo node = get(0).getAccessibilityNode();

    try {
      AccessibilityNodeInfo root = ScreenUtilities.findRootNode(node);

      if (root != null) {
        NodeComparator comparator = new NodeComparator() {
          private final Map<AccessibilityNodeInfo, Rect> nodeLocations =
                new HashMap<AccessibilityNodeInfo, Rect>();

          private final Rect getLocation (AccessibilityNodeInfo node) {
            synchronized (nodeLocations) {
              Rect location = nodeLocations.get(node);
              if (location != null) return location;

              location = new Rect();
              node.getBoundsInScreen(location);

              nodeLocations.put(node, location);
              return location;
            }
          }

          @Override
          public int compare (AccessibilityNodeInfo node1, AccessibilityNodeInfo node2) {
            if (node1 == null) {
              return (node2 == null)? 0: 1;
            } else if (node2 == null) {
              return -1;
            }

            Rect location1 = getLocation(node1);
            Rect location2 = getLocation(node2);

            return (location1.top < location2.top)? -1:
                   (location1.top > location2.top)? 1:
                   (location1.left < location2.left)? -1:
                   (location1.left > location2.left)? 1:
                   (location1.right > location2.right)? -1:
                   (location1.right < location2.right)? 1:
                   (location1.bottom > location2.bottom)? -1:
                   (location1.bottom < location2.bottom)? 1:
                   0;
          }
        };

        clear();
        addByContainer(comparator, root);
      }
    } finally {
      node.recycle();
      node = null;
    }
  }

  private ScreenElement firstElement = null;
  private ScreenElement lastElement = null;

  public final ScreenElement getFirstElement () {
    return firstElement;
  }

  public final ScreenElement getLastElement () {
    return lastElement;
  }

  private final void linkElements () {
    int count = size();
    if (count == 0) return;

    firstElement = get(0);
    lastElement = get(count-1);
    ScreenElement previousElement = lastElement;

    for (ScreenElement nextElement : this) {
      nextElement.setPreviousElement(previousElement);
      previousElement.setNextElement(nextElement);
      previousElement = nextElement;
    }
  }

  public final void finish () {
    sortByVisualLocation();
    linkElements();
  }

  public final ScreenElement findByBrailleLocation (int column, int row) {
    ScreenElement bestElement = null;
    int bestLeft = -1;

    for (ScreenElement element : this) {
      Rect location = element.getBrailleLocation();
      if (location == null) continue;

      if (row < location.top) continue;
      if (row > location.bottom) continue;

      if (column > location.right) continue;
      if (column >= location.left) return element;

      if (location.left > bestLeft) {
        bestLeft = location.left;
        bestElement = element;
      }
    }

    return bestElement;
  }
}
