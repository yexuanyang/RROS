pipeline {
    agent { dockerfile true }
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
