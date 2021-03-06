/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code is subject to the terms of the Mozilla Public License
 * version 2.0 (the "License"). You can obtain a copy of the License at
 * http://mozilla.org/MPL/2.0/. */

/* base class for ruby rendering objects that directly contain content */

#ifndef nsRubyContentFrame_h___
#define nsRubyContentFrame_h___

#include "nsInlineFrame.h"

typedef nsInlineFrame nsRubyContentFrameSuper;

class nsRubyContentFrame : public nsRubyContentFrameSuper
{
public:
  NS_DECL_FRAMEARENA_HELPERS

  // nsIFrame overrides
  virtual bool IsFrameOfType(uint32_t aFlags) const MOZ_OVERRIDE;

  // Indicates whether this is an "intra-level whitespace" frame, i.e.
  // an anonymous frame that was created to contain non-droppable
  // whitespaces directly inside a ruby level container. This impacts
  // ruby pairing behavior.
  // See http://dev.w3.org/csswg/css-ruby/#anon-gen-interpret-space
  bool IsIntraLevelWhitespace() const;

protected:
  explicit nsRubyContentFrame(nsStyleContext* aContext)
    : nsRubyContentFrameSuper(aContext) {}
};

#endif /* nsRubyContentFrame_h___ */
