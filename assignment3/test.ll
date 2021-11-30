; ModuleID = 'test.cpp'
source_filename = "test.cpp"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@.str = private unnamed_addr constant [24 x i8] c"%d, %d, %d, %d, %d, %d\0A\00", align 1

; Function Attrs: noinline norecurse uwtable
define dso_local i32 @main() #0 {
entry:
  %retval = alloca i32, align 4
  %a = alloca i32, align 4
  %b = alloca i32, align 4
  %c = alloca i32, align 4
  %d = alloca i32*, align 8
  %e = alloca i32*, align 8
  %f = alloca i32, align 4
  store i32 0, i32* %retval, align 4
  
  store i32 0, i32* %a, align 4         # *a = 0
  store i32 1, i32* %b, align 4         # *b = 1
  %0 = load i32, i32* %b, align 4       # %0 = *b
  store i32 %0, i32* %a, align 4        # *a = %0
  
  store i32 0, i32* %c, align 4         # *c = 0
  store i32* %b, i32** %d, align 8      # *d = b
  %1 = load i32*, i32** %d, align 8     # %1 = *d
  %2 = load i32, i32* %1, align 4       # %2 = *%1
  store i32 %2, i32* %c, align 4        # *c = %2
  
  store i32* %b, i32** %e, align 8      # *e = b
  store i32 0, i32* %f, align 4         # *f = 0
  %3 = load i32, i32* %f, align 4       # %3 = *f
  %4 = load i32*, i32** %e, align 8     # %4 = *e
  store i32 %3, i32* %4, align 4        # *%4 = %3
 
  %5 = load i32, i32* %a, align 4
  %6 = load i32, i32* %b, align 4
  %7 = load i32, i32* %c, align 4
  %8 = load i32*, i32** %d, align 8
  %9 = load i32, i32* %8, align 4
  %10 = load i32*, i32** %e, align 8
  %11 = load i32, i32* %10, align 4
  %12 = load i32, i32* %f, align 4
  %call = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([24 x i8], [24 x i8]* @.str, i64 0, i64 0), i32 %5, i32 %6, i32 %7, i32 %9, i32 %11, i32 %12)
  ret i32 0
}

declare dso_local i32 @printf(i8*, ...) #1

attributes #0 = { noinline norecurse uwtable "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "frame-pointer"="all" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "frame-pointer"="all" "less-precise-fpmad"="false" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }

!llvm.module.flags = !{!0}
!llvm.ident = !{!1}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{!"clang version 10.0.0 "}
