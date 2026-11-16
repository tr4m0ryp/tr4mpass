# FYI

## Regular notification example
Next time you open this project, a noticiation will pop up with the following message:
> Example notification from markdown file #regular-notification-example-1

# Details

## Regular notification example

#### What is it?

The `fyi.md` file is how you and any other maintainer of this codebase can _schedule_ `notifications`
to show up next time someone opens the project in `vscode`.

#### How does it work?

Any `quote` (line starting with `>`) you add between the `# FYI` and `# Details` __headers__ in this markdown file will turn into a scheduled notification.

When it pops up, the notification will have at least 1 of 3 buttons / actions:
+ `Learn More`: To open this here file
+ `Got it`: To permanently dismiss the notifcation
+ `Not now`: To temporarily dismiss the notification

#### Syntax

The `FYI.md` file **must** be named as such, and **must** always have an `# FYI` and a `# Details` header, in that order, anywhere in the file.

Any quote (single or multi line) found between these headers will turn into a notification.

Quotes starting with `(!)` are considered `crucial` notifications, and as such won't be a discreet pop-up in vscode, but a very-much-in-your-face modal instead.

For the `Learn More` button to appear, the quote **must** _end with_ an `anchor`.

`Anchors` are the exact same thing as what you would put in standard markdown  
eg: the `#syntax` in a `[link](#syntax)`.

#### Best practices
+ Limit the number of concurrent notifications to `3`  
  (FYI doesn't enforce any limit - the _F_ stands for _Footgun_)
+ Use `crucial` notifications _judiciously_ and _sparingly_
+ Keep the content of a notification succinct + elaborate in its [Details](#details) section
+ The surest way to make sure you get the syntax right for an `anchor` is to start by drafting an actual link, test it out, then just remove the fluff.  
  eg: actually type out `> notification here [details](#details)`  
  so that you get intellisense suggestions for the anchor, then delete the `[details](` and `)`.
+ Add headers that mirror each other in the FYI and Detais section.  
  eg: `## Regular notification example` [here](#regular-notification-example) and [here](#regular-notification-example-1).  
  This becomes especially useful if you have multiple notifications.

#### Other features
- If you have permanently dismissed a notification, but would actually want it to come up again next time you open the project, you can run the `FYI: Reset Dismissed Notifications` command (`SHIFT`+`CMD`+`p`)
- Anytime the `FYI.md` file gets updated, the list of permanently dismissed notifications is reset, meaning that everyone will see the notifications again (even if the change you made isn't affecting the existing quotes)
