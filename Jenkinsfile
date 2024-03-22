pipeline {
    agent { dockerfile true }
    stages {
        stage('Build') {
            steps {
		sh '''
		   clang --version
		   ld.lld --version
		   make LLVM=1 rros_defconfig
		   make LLVM=1 -j
		        '''
            }
        }
    }
}
