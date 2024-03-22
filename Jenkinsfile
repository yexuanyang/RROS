pipeline {
    agent {
	docker { image 'ubuntu:22.04' }
    }
    stages {
        stage('Build') {
            steps {
                sh 'echo "Hello World"'
                sh '''
                    echo "Multiline shell steps works too"
                    ls -lah
                    docker run -it 
                '''
            }
        }
    }
}
