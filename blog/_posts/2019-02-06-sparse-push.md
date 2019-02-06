---
layout: post
title: "Speeding up 'git push' for developers"
author: "Derrick Stolee"
tags: [performance, config]
excerpt_separator: <!--more-->
---

The algorithm Git uses to determine objects to send during a push
from a client machine is the same algorithm it uses to select
objects to send during a fetch from a server machine. This
algorithm has some performance issues when used in a normal
developer workflow, and can be sped up using a new algorithm.

We'll discuss the difference in the algorithms and show how to
enable the new algorithm on your machine.

<!--more-->

# What does `git push` do?

When you run `git push` as a developer, Git may output
something like this:

```
$ git push origin topic
Enumerating objects: 3670, done.
Counting objects: 100% (2369/2369), done.
Delta compression using up to 8 threads
Compressing objects: 100% (546/546), done.
Writing objects: 100% (1378/1378), 468.06 KiB | 7.67 MiB/s, done.
Total 1378 (delta 1109), reused 1096 (delta 832)
remote: Resolving deltas: 100% (1109/1109), completed with 312 local objects.
To https://server.info/fake.git
 * [new branch]            topic -> topic

```

That's a lot of data to process as a user. Today I
want to focus on the "Enumerating Objects" phase.

# Old Algorithm

The existing algorithm does the following:

1. Compute the commit frontier.

2. Walk all reachable objects starting at the root trees
   of uninteresting commits; mark these objects as
   uninteresting.

3. Walk all reachable objects starting at the interesting
   commits, but not walking beyond uninteresting objects.

# New Algorithm

The new algorithm uses paths.

# How to use

To enable this on your machine, run the following config command:

```
git config --global pack.useSparse true
```

This enables the new "sparse" algorithm when constructing a pack
during the underlying `git pack-objects` command in a push.

To avoid issues with extra objects, you can disable this config
setting for a single push by using the `-c` flag:

```
git -c pack.useSparse=false push origin topic
```

You should only need to disable this config setting if you are
doing some very serious refactoring that leads to exact copies
of folders changing parent folders.

