; ModuleID = './test/exit_code.laye'
source_filename = "./test/exit_code.laye"

define void @main() {
entry:
  call void @exit(i32 69)
  unreachable
}

declare void @exit()

