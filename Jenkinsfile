pipeline {
    agent any
    stages {
        stage('Build') {
            steps {
		sh '''
		   clang --version
		   ld.lld --version
		'''
            }
        }
    }
}
