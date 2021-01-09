### dropped
omegaflow:
b0104b1e220837b0f01a95e4228d6ed6a5e5c1b8
Fix: implement chdir, mkdirat, renameat2

b529b76b3a81124e924c7861f0b2bc9be70e0de9
fix load reserved by treating it like non-spec insts
a55d5264624558e067ec5525900c2717ee297ef4
Dont verify RV AMO load, because it is not verifiable
76a52197a4e5d2a1a2131ff44c847faf026556f2
fix strange shape of amo store

IsRVAmoLoadHalf
IsRVAmoStoreHalf

### must implement in omegaflow
IsRVAmoStoreHalf -> IsAtomic

### Important:
c21bef6d4f9788bba51a45ee2192504020ba0e04
fixed major memory corruption in NoSQ SMB

+            translationStarted(false);
+            translationCompleted(false);
+            loadIssueCount++;

-        execCount++;
+        wbCount++;
